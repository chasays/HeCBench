/*******************************************************************************
 * CLI - A simple command line interface.
 * Copyright (C) 2016-2021 Daniele Pallastrelli
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ******************************************************************************/

#ifndef CLI_DETAIL_NEWSTANDALONEASIOLIB_H_
#define CLI_DETAIL_NEWSTANDALONEASIOLIB_H_

#include <asio/version.hpp>
#include <asio.hpp>

namespace cli
{
namespace detail
{

namespace asiolib = asio;
namespace asiolibec = asio;

class NewStandaloneAsioLib
{
public:

    using ContextType = asio::io_context;

    class Executor
    {
    public:
        explicit Executor(ContextType& ios) :
            executor(ios.get_executor()) {}
        explicit Executor(asio::ip::tcp::socket& socket) :
            executor(socket.get_executor()) {}
        template <typename T> void Post(T&& t) { asio::post(executor, std::forward<T>(t)); }
    private:
#if ASIO_VERSION >= 101700
    using AsioExecutor = asio::any_io_executor;
#else
    using AsioExecutor = asio::executor;
#endif
         AsioExecutor executor;
    };

    static asio::ip::address IpAddressFromString(const std::string& address)
    {
        return asio::ip::make_address(address);
    }

    static auto MakeWorkGuard(ContextType& context)
    {
        return asio::make_work_guard(context);
    }
};

} // namespace detail
} // namespace cli

#endif // CLI_DETAIL_NEWSTANDALONEASIOLIB_H_

