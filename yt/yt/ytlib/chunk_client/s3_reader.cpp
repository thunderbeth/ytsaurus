#include "s3_reader.h"

#include "chunk_reader.h"
#include "chunk_layout_facade.h"
#include "chunk_reader_allowing_repair.h"
#include "config.h"

// TODO(achulkov2): Move these includes to layout facacde file.
#include <yt/yt/ytlib/chunk_client/format.h>
#include <yt/yt/core/misc/checksum.h>

#include <yt/yt/ytlib/chunk_client/chunk_meta_extensions.h>

namespace NYT::NChunkClient {

using namespace NConcurrency;
using namespace NThreading;

////////////////////////////////////////////////////////////////////////////

// TODO(achulkov2): Keep in mind that we want IChunkReaderAllowingRepair.
// We will need to store the slowness checker, apply it at the right place
// (where?) and start returning correct failure times.
class TS3Reader
    : public IChunkReader
{
public:
    TS3Reader(
        TS3MediumStubPtr medium,
        TS3ReaderConfigPtr config,
        TChunkId chunkId)
        : Medium_(std::move(medium))
        , Client_(Medium_->GetClient())
        , Config_(std::move(config))
        , ChunkId_(std::move(chunkId))
        , ChunkPlacement_(Medium_->GetChunkPlacement(ChunkId_))
    {
    }

    TFuture<std::vector<TBlock>> ReadBlocks(
        const TReadBlocksOptions& options,
        const std::vector<int>& blockIndexes) override
    {
        // TODO(achulkov2): Improve this code to aggregate ranges of consecutive blocks.
        auto blockRanges = std::vector<TBlockRange>{};
        blockRanges.reserve(blockIndexes.size());
        for (const auto blockIndex : blockIndexes) {
            blockRanges.push_back({.StartBlockIndex = blockIndex, .EndBlockIndex = blockIndex + 1});
        }
        return ReadBlockRanges(options, blockRanges);
    }

    // TODO(achulkov2): Use session invoker all over the place.
    TFuture<std::vector<TBlock>> ReadBlocks(
        const TReadBlocksOptions& options,
        int firstBlockIndex,
        int blockCount) override
    {
        auto blockRange = TBlockRange{
            .StartBlockIndex = firstBlockIndex,
            .EndBlockIndex = firstBlockIndex + blockCount
        };
        return ReadBlockRanges(options, {blockRange});
    }

    // TODO(achulkov2): What invoker should we use here? Maybe it should be introduced, idk.
    // But we can start with GetCurrentInvoker probably.
    TFuture<TRefCountedChunkMetaPtr> GetMeta(
        const TGetMetaOptions& options,
        std::optional<int> partitionTag = std::nullopt,
        const std::optional<std::vector<int>>& extensionTags = {}) override
    {
        // TODO(achulkov2): Support options, partition tag and extension tags.
        // TODO(achulkov2): Do not forget about statistics.
        Y_UNUSED(options);
        Y_UNUSED(partitionTag);
        Y_UNUSED(extensionTags);

        return ReadMeta();
    }

    TChunkId GetChunkId() const override
    {
        return ChunkId_;
    }

    TInstant GetLastFailureTime() const override
    {
        return LastFailureTime_.load();
    }

private:
    const TS3MediumStubPtr Medium_;
    const NS3::IClientPtr Client_;
    const TS3ReaderConfigPtr Config_;
    const TChunkId ChunkId_;
    const TS3MediumStub::TS3ObjectPlacement ChunkPlacement_;

    // TODO(achulkov2): Do we want to cache meta somehow?
    YT_DECLARE_SPIN_LOCK(TReaderWriterSpinLock, MetaLock_);
    TRefCountedChunkMetaPtr ChunkMeta_;
    TBlocksExtPtr BlocksExt_;

    // TODO(achulkov2): Set this on failures.
    std::atomic<TInstant> LastFailureTime_ = TInstant::Zero();

    struct TBlockRange
    {
        //! Inclusive.
        int StartBlockIndex = 0;
        //! Not inclusive.
        int EndBlockIndex = 0;
    };

    TFuture<std::vector<TBlock>> ReadBlockRanges(
        const TReadBlocksOptions& options,
        const std::vector<TBlockRange>& blockRanges,
        const TBlocksExtPtr& blocksExt = nullptr)
    {
        if (!blocksExt) {
            return ReadBlocksExt()
                .Apply(BIND(&TS3Reader::ReadBlockRanges, MakeStrong(this), options, blockRanges)); // TODO(achulkov2): Invoker.
        }

        std::vector<TFuture<std::vector<TBlock>>> futures;
        futures.reserve(blockRanges.size());

        for (const auto& blockRange : blockRanges) {
            futures.push_back(ReadBlockRange(options, blockRange, blocksExt));
        }

        return AllSet(std::move(futures))
            .Apply(BIND([] (const std::vector<TErrorOr<std::vector<TBlock>>>& results) { // TODO(achulkov2): Invoker.
                std::vector<TBlock> blocks;
                for (const auto& result : results) {
                    const auto& rangeBlocks = result.ValueOrThrow();
                    blocks.insert(blocks.end(), rangeBlocks.begin(), rangeBlocks.end());
                }
                return blocks;
            }));
    }

    // TODO(achulkov2): Move interactions with blocks, checksum validations, etc, to a class shared with chunk_file_writer.

    TFuture<std::vector<TBlock>> ReadBlockRange(
        const TReadBlocksOptions& options,
        const TBlockRange& blockRange,
        const TBlocksExtPtr& blocksExt)
    {
        // TODO(achulkov2): Use these options.
        Y_UNUSED(options);

        // TODO(achulkov2): This should probably be checked while forming ranges.
        if (blockRange.StartBlockIndex < 0 || blockRange.EndBlockIndex < blockRange.StartBlockIndex) {
            THROW_ERROR_EXCEPTION("Invalid block range: [%v, %v)",
                blockRange.StartBlockIndex,
                blockRange.EndBlockIndex);
        }

        // TODO(achulkov2): Improve message here, at least add path to chunk file.
        if (blockRange.EndBlockIndex > std::ssize(blocksExt->Blocks)) {
            THROW_ERROR_EXCEPTION("Block range [%v, %v) is out of bounds: only %v blocks exist",
                blockRange.StartBlockIndex,
                blockRange.EndBlockIndex,
                std::ssize(blocksExt->Blocks));
        }

        auto firstBlockInfo = blocksExt->Blocks[blockRange.StartBlockIndex];
        auto lastBlockInfo = blocksExt->Blocks[blockRange.EndBlockIndex - 1];
        auto totalSize = lastBlockInfo.Offset - firstBlockInfo.Offset + lastBlockInfo.Size;

        NS3::TGetObjectRequest request;
        request.Bucket = ChunkPlacement_.Bucket;
        request.Key = ChunkPlacement_.Key;
        request.Range = Format("bytes=%v-%v", firstBlockInfo.Offset, firstBlockInfo.Offset + totalSize - 1);

        return Client_->GetObject(request)
            .Apply(BIND([totalSize, blockRange, firstBlockInfo, blocksExt] (const NS3::TGetObjectResponse& response) { // TODO(achulkov2): Invoker.
                // TODO(achulkov2): Better error.
                if (std::ssize(response.Data) != totalSize) {
                    THROW_ERROR_EXCEPTION("Incorrect data size: expected %v, actual %v",
                        totalSize,
                        response.Data.Size());
                }

                std::vector<TBlock> blocks;
                blocks.reserve(blockRange.EndBlockIndex - blockRange.StartBlockIndex);

                for (int blockIndex = blockRange.StartBlockIndex; blockIndex < blockRange.EndBlockIndex; ++blockIndex) {
                    auto blockInfo = blocksExt->Blocks[blockIndex];
                    auto block = response.Data.Slice(blockInfo.Offset - firstBlockInfo.Offset, blockInfo.Size);
                    blocks.push_back(TBlock(block, blockInfo.Checksum));
                }

                return blocks;
            }));
    }

    // TODO(achulkov2): Think about read vs fetch in name.
    TFuture<TRefCountedChunkMetaPtr> ReadMeta()
    {
        {
            auto guard = ReaderGuard(MetaLock_);

            if (ChunkMeta_) {
                return MakeFuture<TRefCountedChunkMetaPtr>(ChunkMeta_);
            }
        }

        auto metaPlacement = Medium_->GetChunkMetaPlacement(ChunkPlacement_);

        NS3::TGetObjectRequest request;
        request.Bucket = metaPlacement.Bucket;
        request.Key = metaPlacement.Key;
        return Client_->GetObject(request)
            .Apply(BIND([this, this_ = MakeStrong(this)] (const NS3::TGetObjectResponse& response) { // TODO(achulkov2): Invoker.
                auto meta = DeserializeMeta(response.Data);

                auto guard = WriterGuard(MetaLock_);

                if (!ChunkMeta_) {
                    ChunkMeta_ = meta;
                    BlocksExt_ = New<TBlocksExt>(GetProtoExtension<NChunkClient::NProto::TBlocksExt>(ChunkMeta_->extensions()));
                }
                return ChunkMeta_;
            }));
    }

    // TODO(achulkov2): Think about read vs fetch in name.
    TFuture<TBlocksExtPtr> ReadBlocksExt()
    {
        return ReadMeta()
            .AsVoid()
            .Apply(BIND([this, this_ = MakeStrong(this)] () { // TODO(achulkov2): Invoker.
                auto guard = ReaderGuard(MetaLock_);
                return BlocksExt_;
            }));
    }

    template <class T>
    void ReadHeader(
        const TSharedRef& metaFileBlob,
        const TString& fileName,
        TChunkMetaHeader_2* metaHeader,
        TRef* metaBlob)
    {
        if (metaFileBlob.Size() < sizeof(T)) {
            THROW_ERROR_EXCEPTION("Chunk meta file %v is too short: at least %v bytes expected",
                fileName,
                sizeof(T));
        }
        *static_cast<T*>(metaHeader) = *reinterpret_cast<const T*>(metaFileBlob.Begin());
        *metaBlob = metaFileBlob.Slice(sizeof(T), metaFileBlob.Size());
    }

    TRefCountedChunkMetaPtr DeserializeMeta(TSharedRef metaFileBlob)
    {
        // TODO(achulkov2): Fill.
        auto metaFileName = "fill_me";

        if (metaFileBlob.Size() < sizeof(TChunkMetaHeaderBase)) {
            THROW_ERROR_EXCEPTION(
                NChunkClient::EErrorCode::BrokenChunkFileMeta,
                "Chunk meta file %v is too short: at least %v bytes expected",
                metaFileName,
                sizeof(TChunkMetaHeaderBase));
        }

        // TODO(achulkov2): Increment statistics?

        TChunkMetaHeader_2 metaHeader;
        TRef metaBlob;
        const auto* metaHeaderBase = reinterpret_cast<const TChunkMetaHeaderBase*>(metaFileBlob.Begin());

        switch (metaHeaderBase->Signature) {
            case TChunkMetaHeader_1::ExpectedSignature:
                ReadHeader<TChunkMetaHeader_1>(metaFileBlob, metaFileName, &metaHeader, &metaBlob);
                metaHeader.ChunkId = ChunkId_;
                break;

            case TChunkMetaHeader_2::ExpectedSignature:
                ReadHeader<TChunkMetaHeader_2>(metaFileBlob, metaFileName, &metaHeader, &metaBlob);
                break;

            default:
                THROW_ERROR_EXCEPTION(
                    NChunkClient::EErrorCode::BrokenChunkFileMeta,
                    "Incorrect header signature %x in chunk meta file %v",
                    metaHeaderBase->Signature,
                    metaFileName);
        }

        auto checksum = GetChecksum(metaBlob);
        if (checksum != metaHeader.Checksum) {
            // DumpBrokenMeta(metaBlob);
            THROW_ERROR_EXCEPTION(
                NChunkClient::EErrorCode::BrokenChunkFileMeta,
                "Incorrect checksum in chunk meta file %v: expected %x, actual %x",
                metaFileName,
                metaHeader.Checksum,
                checksum)
                << TErrorAttribute("meta_file_length", metaFileBlob.Size());
        }

        // TODO(achulkov2): Verify that chunk id is not null somewhere.
        if (metaHeader.ChunkId != ChunkId_) {
            THROW_ERROR_EXCEPTION("Invalid chunk id in meta file %v: expected %v, actual %v",
                metaFileName,
                ChunkId_,
                metaHeader.ChunkId);
        }

        NProto::TChunkMeta meta;
        if (!TryDeserializeProtoWithEnvelope(&meta, metaBlob)) {
            THROW_ERROR_EXCEPTION("Failed to parse chunk meta file %v",
                metaFileName);
        }

        return New<TRefCountedChunkMeta>(std::move(meta));

    }
};



////////////////////////////////////////////////////////////////////////////

IChunkReaderPtr CreateS3Reader(
    TS3MediumStubPtr medium,
    TS3ReaderConfigPtr config,
    TChunkId chunkId)
{
    if (!medium) {
        medium = CreateTestS3Medium();
    }

    if (!config) {
        config = New<TS3ReaderConfig>();
    }

    return New<TS3Reader>(
        std::move(medium),
        std::move(config),
        std::move(chunkId));
}

////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient