#include <Server/InterserverIOHTTPHandler.h>

#include <Server/IServer.h>

#include <Compression/CompressedWriteBuffer.h>
#include <Core/ServerSettings.h>
#include <IO/ReadBufferFromIStream.h>
#include <Interpreters/Context.h>
#include <Interpreters/InterserverIOHandler.h>
#include <Server/HTTP/HTMLForm.h>
#include <Server/HTTP/WriteBufferFromHTTPServerResponse.h>
#include <Common/logger_useful.h>
#include <Common/setThreadName.h>

#include <DBPoco/Net/HTTPBasicCredentials.h>
#include <DBPoco/Util/LayeredConfiguration.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int ABORTED;
    extern const int TOO_MANY_SIMULTANEOUS_QUERIES;
}

std::pair<String, bool> InterserverIOHTTPHandler::checkAuthentication(HTTPServerRequest & request) const
{
    auto server_credentials = server.context()->getInterserverCredentials();
    if (server_credentials)
    {
        if (!request.hasCredentials())
            return server_credentials->isValidUser("", "");

        String scheme, info;
        request.getCredentials(scheme, info);

        if (scheme != "Basic")
            return {"Server requires HTTP Basic authentication but client provides another method", false};

        DBPoco::Net::HTTPBasicCredentials credentials(info);
        return server_credentials->isValidUser(credentials.getUsername(), credentials.getPassword());
    }
    else if (request.hasCredentials())
    {
        return {"Client requires HTTP Basic authentication, but server doesn't provide it", false};
    }

    return {"", true};
}

void InterserverIOHTTPHandler::processQuery(HTTPServerRequest & request, HTTPServerResponse & response, Output & used_output)
{
    HTMLForm params(server.context()->getSettingsRef(), request);

    LOG_TRACE(log, "Request URI: {}", request.getURI());

    String endpoint_name = params.get("endpoint");
    bool compress = params.get("compress") == "true";

    auto & body = request.getStream();

    auto endpoint = server.context()->getInterserverIOHandler().getEndpoint(endpoint_name);
    /// Locked for read while query processing
    std::shared_lock lock(endpoint->rwlock);
    if (endpoint->blocker.isCancelled())
        throw Exception(ErrorCodes::ABORTED, "Transferring part to replica was cancelled");

    if (compress)
    {
        CompressedWriteBuffer compressed_out(*used_output.out);
        endpoint->processQuery(params, body, compressed_out, response);
    }
    else
    {
        endpoint->processQuery(params, body, *used_output.out, response);
    }
}


void InterserverIOHTTPHandler::handleRequest(HTTPServerRequest & request, HTTPServerResponse & response, const ProfileEvents::Event & write_event)
{
    setThreadName("IntersrvHandler");

    /// In order to work keep-alive.
    if (request.getVersion() == HTTPServerRequest::HTTP_1_1)
        response.setChunkedTransferEncoding(true);

    Output used_output;
    used_output.out = std::make_shared<WriteBufferFromHTTPServerResponse>(
        response, request.getMethod() == DBPoco::Net::HTTPRequest::HTTP_HEAD, write_event);

    auto finalize_output = [&]
    {
        try
        {
            used_output.out->finalize();
        }
        catch (...)
        {
            tryLogCurrentException(log, "Failed to finalize response write buffer");
        }
    };

    auto write_response = [&](const std::string & message)
    {
        if (response.sent())
        {
            finalize_output();
            return;
        }

        try
        {
            writeString(message, *used_output.out);
            finalize_output();
        }
        catch (...)
        {
            tryLogCurrentException(log);
            finalize_output();
        }
    };

    try
    {
        if (auto [message, success] = checkAuthentication(request); success)
        {
            processQuery(request, response, used_output);
            finalize_output();
            LOG_DEBUG(log, "Done processing query");
        }
        else
        {
            response.setStatusAndReason(HTTPServerResponse::HTTP_UNAUTHORIZED);
            write_response(message);
            LOG_WARNING(log, "Query processing failed request: '{}' authentication failed", request.getURI());
        }
    }
    catch (Exception & e)
    {
        if (e.code() == ErrorCodes::TOO_MANY_SIMULTANEOUS_QUERIES)
        {
            used_output.out->finalize();
            return;
        }

        response.setStatusAndReason(DBPoco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);

        /// Sending to remote server was cancelled due to server shutdown or drop table.
        bool is_real_error = e.code() != ErrorCodes::ABORTED;

        PreformattedMessage message = getCurrentExceptionMessageAndPattern(is_real_error);
        write_response(message.text);

        if (is_real_error)
            LOG_ERROR(log, message);
        else
            LOG_INFO(log, message);
    }
    catch (...)
    {
        response.setStatusAndReason(DBPoco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
        PreformattedMessage message = getCurrentExceptionMessageAndPattern(/* with_stacktrace */ false);
        write_response(message.text);

        LOG_ERROR(log, message);
    }
}


}
