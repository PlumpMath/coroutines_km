#include "common.h"

#include "future_promise.h"

namespace simple {

struct CoroWriteAwaiter : AsyncWriterBase {
    CoroWriteAwaiter(HANDLE handle, const char* data, size_t size)
        : AsyncWriterBase(handle)
        , m_coroutineHandle()
        , m_data(data)
        , m_size(size) {
    }

    bool await_ready() {
        return false;
    }

    void await_suspend(std::experimental::coroutine_handle<coro::promise<void>> coroutineHandle) {
        m_coroutineHandle = coroutineHandle;
        StartAsyncWrite(m_data, m_size);
    }

    DWORD await_resume() {
        return m_bytesWritten;
    }

private:
    std::experimental::coroutine_handle<coro::promise<void>> m_coroutineHandle;
    const void* m_data;
    size_t m_size;
    DWORD m_bytesWritten;

    virtual void WriteFinished(DWORD bytesWritten) {
        m_bytesWritten = bytesWritten;
        m_coroutineHandle.resume();
    }
};

struct AsyncWriter {
    explicit AsyncWriter(HANDLE file)
        : m_handle(file) {
    }

    coro::future<void> WriteAsync(std::string message) {
        std::cout << "starting ..." << std::endl;
        auto bytesWritten = co_await CoroWriteAwaiter{m_handle, message.data(), message.size()};
        std::cout << "done, bytes written: " << bytesWritten << std::endl;
    }

    HANDLE m_handle;
};

} // namespace simple

using namespace simple;

void TrySimpleCoroWaiter() {
    auto file = ::CreateFile(TEXT("2.txt"), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, nullptr);
    AsyncWriter asyncWriter{file};

    auto future = asyncWriter.WriteAsync("hello coroutines!");
    future.get_value();
}