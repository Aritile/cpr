#ifndef CPR_THREAD_POOL_H
#define CPR_THREAD_POOL_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

#define CPR_DEFAULT_THREAD_POOL_MAX_THREAD_NUM std::thread::hardware_concurrency()

constexpr size_t CPR_DEFAULT_THREAD_POOL_MIN_THREAD_NUM = 1;
constexpr std::chrono::milliseconds CPR_DEFAULT_THREAD_POOL_MAX_IDLE_TIME{250};

namespace cpr {

class ThreadPool {
  public:
    using Task = std::function<void()>;

    explicit ThreadPool(size_t min_threads = CPR_DEFAULT_THREAD_POOL_MIN_THREAD_NUM, size_t max_threads = CPR_DEFAULT_THREAD_POOL_MAX_THREAD_NUM, std::chrono::milliseconds max_idle_ms = CPR_DEFAULT_THREAD_POOL_MAX_IDLE_TIME);
    ThreadPool(const ThreadPool& other) = delete;
    ThreadPool(ThreadPool&& old) = delete;

    virtual ~ThreadPool();

    ThreadPool& operator=(const ThreadPool& other) = delete;
    ThreadPool& operator=(ThreadPool&& old) = delete;

    void SetMinThreadNum(size_t min_threads) {
        min_thread_num = min_threads;
    }

    void SetMaxThreadNum(size_t max_threads) {
        max_thread_num = max_threads;
    }

    void SetMaxIdleTime(std::chrono::milliseconds ms) {
        max_idle_time = ms;
    }

    size_t GetCurrentThreadNum() {
        return cur_thread_num;
    }

    size_t GetIdleThreadNum() {
        return idle_thread_num;
    }

    bool IsStarted() const {
        return status != STOP;
    }

    bool IsStopped() const {
        return status == STOP;
    }

    int Start(size_t start_threads = 0);
    int Stop();
    int Pause();
    int Resume();
    int Wait() const;

    /**
     * Return a future, calling future.get() will wait task done and return RetType.
     * Submit(fn, args...)
     * Submit(std::bind(&Class::mem_fn, &obj))
     * Submit(std::mem_fn(&Class::mem_fn, &obj))
     **/
    template <class Fn, class... Args>
    auto Submit(Fn&& fn, Args&&... args) {
        if (status == STOP) {
            Start();
        }
        if (idle_thread_num <= 0 && cur_thread_num < max_thread_num) {
            CreateThread();
        }
        using RetType = decltype(fn(args...));
        auto task = std::make_shared<std::packaged_task<RetType()>>([fn = std::forward<Fn>(fn), args...]() mutable { return std::invoke(fn, args...); });
        std::future<RetType> future = task->get_future();
        {
            std::lock_guard<std::mutex> locker(task_mutex);
            tasks.emplace([task] { (*task)(); });
        }

        task_cond.notify_one();
        return future;
    }

  private:
    bool CreateThread();
    void AddThread(const std::shared_ptr<std::thread>& thread);
    void DelThread(std::thread::id id);

  public:
    size_t min_thread_num;
    size_t max_thread_num;
    std::chrono::milliseconds max_idle_time;

  private:
    enum Status {
        STOP,
        RUNNING,
        PAUSE,
    };

    struct ThreadData {
        std::shared_ptr<std::thread> thread;
        std::thread::id id;
        Status status;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point stop_time;
    };

    std::atomic<Status> status{Status::STOP};
    std::condition_variable status_wait_cond{};
    std::mutex status_wait_mutex{};

    std::atomic<size_t> cur_thread_num{0};
    std::atomic<size_t> idle_thread_num{0};

    std::list<ThreadData> threads{};
    std::mutex thread_mutex{};

    std::queue<Task> tasks{};
    std::mutex task_mutex{};
    std::condition_variable task_cond{};
};

} // namespace cpr

#endif
