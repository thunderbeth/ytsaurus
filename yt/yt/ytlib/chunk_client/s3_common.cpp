#include "s3_common.h"
#include "private.h"
#include "session_id.h"

#include <yt/yt/core/bus/tcp/dispatcher.h>
#include <yt/yt/ytlib/chunk_client/dispatcher.h>

namespace NYT::NChunkClient {

////////////////////////////////////////////////////////////////////////////

TS3MediumStub::TS3MediumStub(
    NS3::IClientPtr client,
    TString bucketName)
    : Client_(std::move(client))
    , BucketName_(std::move(bucketName))
{
    YT_VERIFY(Client_);
}

NS3::IClientPtr TS3MediumStub::GetClient() const
{
    return Client_;
}

TS3MediumStub::TS3ObjectPlacement TS3MediumStub::GetChunkPlacement(TChunkId chunkId) const
{
    return {
        .Bucket = BucketName_,
        .Key = Format("chunk-data/%v", chunkId)
    };
}

TS3MediumStub::TS3ObjectPlacement TS3MediumStub::GetChunkMetaPlacement(const TS3ObjectPlacement& chunkPlacement)
{
    return {
        .Bucket = chunkPlacement.Bucket,
        .Key = chunkPlacement.Key + ChunkMetaSuffix,
    };
}

////////////////////////////////////////////////////////////////////////////

TS3MediumStubPtr CreateTestS3Medium()
{
    auto clientConfig = New<NS3::TS3ClientConfig>();
    // TODO(achulkov2): Fill credentials.
    clientConfig->Url = "http://localhost:9000";
    clientConfig->Region = "us-east-1";
    clientConfig->AccessKeyId = "admin";
    clientConfig->SecretAccessKey = "password";
    auto s3Client = NS3::CreateClient(clientConfig, NYT::NBus::TTcpDispatcher::Get()->GetXferPoller(), TDispatcher::Get()->GetWriterInvoker());
    NConcurrency::WaitFor(s3Client->Start())
        .ThrowOnError();
    return New<TS3MediumStub>(s3Client, "test-bucket");
}

////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient