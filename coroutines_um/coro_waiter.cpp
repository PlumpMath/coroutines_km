#include "common.h"


#include <experimental/resumable>

#include <future>

#include "future_promise.h"

namespace has_exceptions
{
#if 0
struct my_promise
{
    my_promise()
        //: m_event()
    {
        CALL_SPY();

        //EXPECT(m_event != nullptr);
    }

    my_promise(const my_promise& ) = delete;
    my_promise& operator=(const my_promise& ) = delete;

    ~my_promise()
    {
        CALL_SPY();
    }

public:
    std::experimental::suspend_never initial_suspend()
    {
        CALL_SPY();
        return {};
    }

    my_future get_return_object();

    std::experimental::suspend_never final_suspend()
    {
        CALL_SPY();
        return {};
    }


public:
    void SetValue()
    {
        CALL_SPY();
    }

private:
    //HANDLE m_event;
};


////////////////////////////////////////////////////////////////////////////////

struct my_future
{
    ~my_future()
    {
        CALL_SPY();
    }

    my_future(my_future&& other) = default;
    my_future& operator=(my_future&& other) = default;

    explicit my_future(HANDLE event)
        : m_event(event)
    {
        CALL_SPY();
        EXPECT(m_event != INVALID_HANDLE_VALUE);
    }

    my_future(const my_future& ) = delete;
    my_future& operator=(const my_future& ) = delete;



    typedef my_promise promise_type;

    void GetValue()
    {
        CALL_SPY();
        EXPECT(WaitForSingleObjectEx(m_event, INFINITE, true) == WAIT_IO_COMPLETION);
        CALL_SPY();
    }
private:
    HANDLE m_event;
};

////////////////////////////////////////////////////////////////////////////////


my_future my_promise::get_return_object()
{
    CALL_SPY();

    auto event = ::CreateEvent(nullptr, true, true, nullptr);
    EXPECT(event != nullptr);

    return my_future{event};
}


////////////////////////////////////////////////////////////////////////////////

#endif //0



class AwaiterBase : private OVERLAPPED
{
public:

    typedef DWORD value_type;

    typedef coro::promise<void> promise_type;

    AwaiterBase()
        : OVERLAPPED()
        , m_coroutineHandle()
        , m_value(0)
    {
        CALL_SPY();
    }

protected:
    std::experimental::coroutine_handle<promise_type> m_coroutineHandle;
    value_type m_value;

    OVERLAPPED* GetOverlapped()
    {
        return static_cast<OVERLAPPED*>(this);
    }

    static AwaiterBase* GetAwaiterBase(OVERLAPPED* overlapped)
    {
        return reinterpret_cast<AwaiterBase*>(overlapped);
    }

    static void WINAPI OnWrite(DWORD error, DWORD bytesWritten, LPOVERLAPPED overlapped)
    {
        AwaiterBase* me = GetAwaiterBase(overlapped);

        me->m_coroutineHandle.promise().set_error(coro::detail::GetErrorCodeFromWindowsResult(error));
        me->m_value = bytesWritten;

        me->m_coroutineHandle();
    }
};

struct WriteAwaiter : AwaiterBase
{
    explicit WriteAwaiter(HANDLE handle)
        : AwaiterBase()
        , m_handle(handle)
        , m_data(DataSize, 'b')
    {
    }

    bool await_ready()
    {
        return false;
    }

    void await_suspend(std::experimental::coroutine_handle<promise_type> coroutineHandle)
    {
        m_coroutineHandle = coroutineHandle;



        auto success = ::WriteFileEx(m_handle, m_data.data(), m_data.size(), GetOverlapped(), OnWrite);
        if (!success)
        {
            m_coroutineHandle.promise().set_error(coro::detail::GetLastErrorCode());
            m_coroutineHandle.destroy();
            return;
        }
    }

    value_type await_resume()
    {
        CALL_SPY();
        return m_value;
    }

private:
    HANDLE m_handle;
    blob_t m_data;
};


WriteAwaiter WriteAsync()
{
    CALL_SPY();
    HANDLE file = ::CreateFile(TEXT("2.txt"),
                               GENERIC_WRITE,
                               FILE_SHARE_READ,
                               nullptr,
                               CREATE_ALWAYS,
                               FILE_FLAG_OVERLAPPED,
                               nullptr);
    EXPECT(file != INVALID_HANDLE_VALUE);


    return WriteAwaiter{file};
}




//std::future<void>
coro::future<void>
WriteAndTrace()
{
    CALL_SPY();
    await WriteAsync();
    
    auto bytesWritten = await WriteAsync();
    TRACE() << "WOW!!! we have result (bytes written): " << bytesWritten << std::endl;
}

} // namespace has_exceptions

void TryCoroWaiter()
{
    using namespace has_exceptions;

    CALL_SPY();
    try
    {
        CALL_SPY();
        auto&& res = WriteAndTrace();
        CALL_SPY();

        auto err = res.get_value();

        if (err)
        {
            TRACE() << "fail with error: " << err.message() << std::endl;
        }
        else
        {
            TRACE() << "finished" << std::endl;
        }


        CALL_SPY();
    }
    catch (const error_info_t& e)
    {
        TRACE() << e.first << ": " << e.second << std::endl;
    }

    CALL_SPY();
}