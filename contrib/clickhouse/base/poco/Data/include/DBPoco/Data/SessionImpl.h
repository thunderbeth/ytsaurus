//
// SessionImpl.h
//
// Library: Data
// Package: DataCore
// Module:  SessionImpl
//
// Definition of the SessionImpl class.
//
// Copyright (c) 2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#ifndef DB_Data_SessionImpl_INCLUDED
#define DB_Data_SessionImpl_INCLUDED


#include "DBPoco/Any.h"
#include "DBPoco/Data/Data.h"
#include "DBPoco/Format.h"
#include "DBPoco/RefCountedObject.h"
#include "DBPoco/String.h"


namespace DBPoco
{
namespace Data
{


    class StatementImpl;


    class Data_API SessionImpl : public DBPoco::RefCountedObject
    /// Interface for Session functionality that subclasses must extend.
    /// SessionImpl objects are noncopyable.
    {
    public:
        static const std::size_t LOGIN_TIMEOUT_INFINITE = 0;
        /// Infinite connection/login timeout.

        static const std::size_t LOGIN_TIMEOUT_DEFAULT = 60;
        /// Default connection/login timeout in seconds.

        static const std::size_t CONNECTION_TIMEOUT_INFINITE = 0;
        /// Infinite connection/login timeout.

        static const std::size_t CONNECTION_TIMEOUT_DEFAULT = CONNECTION_TIMEOUT_INFINITE;
        /// Default connection/login timeout in seconds.

        SessionImpl(const std::string & connectionString, std::size_t timeout = LOGIN_TIMEOUT_DEFAULT);
        /// Creates the SessionImpl.

        virtual ~SessionImpl();
        /// Destroys the SessionImpl.

        virtual StatementImpl * createStatementImpl() = 0;
        /// Creates a StatementImpl.

        virtual void open(const std::string & connectionString = "") = 0;
        /// Opens the session using the supplied string.
        /// Can also be used with default empty string to reconnect
        /// a disconnected session.
        /// If the connection is not established within requested timeout
        /// (specified in seconds), a ConnectionFailedException is thrown.
        /// Zero timeout means indefinite

        virtual void close() = 0;
        /// Closes the connection.

        virtual bool isConnected() = 0;
        /// Returns true if session is connected, false otherwise.

        void setLoginTimeout(std::size_t timeout);
        /// Sets the session login timeout value.

        std::size_t getLoginTimeout() const;
        /// Returns the session login timeout value.

        virtual void setConnectionTimeout(std::size_t timeout) = 0;
        /// Sets the session connection timeout value.

        virtual std::size_t getConnectionTimeout() = 0;
        /// Returns the session connection timeout value.

        void reconnect();
        /// Closes the connection and opens it again.

        virtual void begin() = 0;
        /// Starts a transaction.

        virtual void commit() = 0;
        /// Commits and ends a transaction.

        virtual void rollback() = 0;
        /// Aborts a transaction.

        virtual bool canTransact() = 0;
        /// Returns true if session has transaction capabilities.

        virtual bool isTransaction() = 0;
        /// Returns true iff a transaction is a transaction is in progress, false otherwise.

        virtual void setTransactionIsolation(DBPoco::UInt32) = 0;
        /// Sets the transaction isolation level.

        virtual DBPoco::UInt32 getTransactionIsolation() = 0;
        /// Returns the transaction isolation level.

        virtual bool hasTransactionIsolation(DBPoco::UInt32) = 0;
        /// Returns true iff the transaction isolation level corresponding
        /// to the supplied bitmask is supported.

        virtual bool isTransactionIsolation(DBPoco::UInt32) = 0;
        /// Returns true iff the transaction isolation level corresponds
        /// to the supplied bitmask.

        virtual const std::string & connectorName() const = 0;
        /// Returns the name of the connector.

        const std::string & connectionString() const;
        /// Returns the connection string.

        static std::string uri(const std::string & connector, const std::string & connectionString);
        /// Returns formatted URI.

        std::string uri() const;
        /// Returns the URI for this session.

        virtual void setFeature(const std::string & name, bool state) = 0;
        /// Set the state of a feature.
        ///
        /// Features are a generic extension mechanism for session implementations.
        /// and are defined by the underlying SessionImpl instance.
        ///
        /// Throws a NotSupportedException if the requested feature is
        /// not supported by the underlying implementation.

        virtual bool getFeature(const std::string & name) = 0;
        /// Look up the state of a feature.
        ///
        /// Features are a generic extension mechanism for session implementations.
        /// and are defined by the underlying SessionImpl instance.
        ///
        /// Throws a NotSupportedException if the requested feature is
        /// not supported by the underlying implementation.

        virtual void setProperty(const std::string & name, const DBPoco::Any & value) = 0;
        /// Set the value of a property.
        ///
        /// Properties are a generic extension mechanism for session implementations.
        /// and are defined by the underlying SessionImpl instance.
        ///
        /// Throws a NotSupportedException if the requested property is
        /// not supported by the underlying implementation.

        virtual DBPoco::Any getProperty(const std::string & name) = 0;
        /// Look up the value of a property.
        ///
        /// Properties are a generic extension mechanism for session implementations.
        /// and are defined by the underlying SessionImpl instance.
        ///
        /// Throws a NotSupportedException if the requested property is
        /// not supported by the underlying implementation.

    protected:
        void setConnectionString(const std::string & connectionString);
        /// Sets the connection string. Should only be called on
        /// disconnetced sessions. Throws InvalidAccessException when called on
        /// a connected session.

    private:
        SessionImpl();
        SessionImpl(const SessionImpl &);
        SessionImpl & operator=(const SessionImpl &);

        std::string _connectionString;
        std::size_t _loginTimeout;
    };


    //
    // inlines
    //
    inline const std::string & SessionImpl::connectionString() const
    {
        return _connectionString;
    }


    inline void SessionImpl::setLoginTimeout(std::size_t timeout)
    {
        _loginTimeout = timeout;
    }


    inline std::size_t SessionImpl::getLoginTimeout() const
    {
        return _loginTimeout;
    }


    inline std::string SessionImpl::uri(const std::string & connector, const std::string & connectionString)
    {
        return format("%s:///%s", connector, connectionString);
    }


    inline std::string SessionImpl::uri() const
    {
        return uri(connectorName(), connectionString());
    }


}
} // namespace DBPoco::Data


#endif // DB_Data_SessionImpl_INCLUDED
