#pragma once
#include <thread>
#include <memory>
#include <mutex>
#include <type_traits>
#include <functional>
#include <future>
#include <atomic>
#include <map>
#include <queue>
#include <condition_variable>
/*
 * thread pool
 *
 */
namespace ThreadPool
{
    /*
     * async task interface class
     */

    class AsyncTask
    {
    public:
        using Ptr = std::shared_ptr<AsyncTask>;
        enum class taskState :std::uint8_t
        {
            waiting = 0,// The task still has not started and is waiting to be executed
            running, // Task is running
            finish, // Task is finish
            invalid // Task IS Invalid
        };
        explicit AsyncTask(const std::size_t taskID, std::function<void()> fn) :m_taskID(taskID)
            , m_Wrapper(std::move(fn))
            , m_state(taskState::waiting) {

        }
        AsyncTask() = delete;
        AsyncTask(const AsyncTask&) = delete;
        AsyncTask(AsyncTask&&) = delete;
        AsyncTask& operator=(const AsyncTask&) = delete;
        AsyncTask& operator=(AsyncTask&&) = delete;

        void run() {
            m_state.store(taskState::running);
            if (m_Wrapper) {
                m_Wrapper();
            }
            m_state.store(taskState::finish);
        }
        virtual ~AsyncTask() = default;
        [[nodiscard]] std::size_t getTaskID() const {
            return m_taskID;
        }
        auto state() const {
            return m_state.load();
        }
        std::size_t addLevel(const std::size_t level) {
            return m_level.fetch_add(level);
        }
        std::size_t subLevel(const std::size_t level) {
            return m_level.fetch_sub(level);
        }
        std::size_t level() const {
            return m_level.load();
        }
        bool abort() {
            if (m_state.load() == taskState::waiting) {
                m_state.store(taskState::invalid);
                return true;
            }
            return false;
        }
    private:
        std::size_t m_taskID;
        std::function<void()> m_Wrapper{ nullptr };
        std::atomic<taskState> m_state{ taskState::invalid };
        std::atomic<std::size_t> m_level{ 0 };
    };
    // compare priority queue
    struct TaskCompare
    {
        bool operator()(const AsyncTask::Ptr& arg1, const AsyncTask::Ptr& arg2) const {
            if (!arg1 || !arg2) return false;
            return arg1->level() > arg2->level();
        }
    };

    template<typename Ry_>
    class ThreadResult final
    {
    public:
        ThreadResult() = delete;
        explicit ThreadResult(const std::size_t& taskID, std::future<Ry_> result) :m_TaskID(taskID)
            , m_future(std::move(result)) {
        }
        ~ThreadResult() = default;
        ThreadResult& operator=(const ThreadResult& other) noexcept {
            m_TaskID = other.taskID();
            m_future = std::move(other.rawFutureResult());
            return *this;
        }
        ThreadResult& operator=(ThreadResult&& other) noexcept {
            m_TaskID = std::move(other.taskID());
            m_future = std::move(other.rawFutureResult());
            return *this;
        }
        ThreadResult(const ThreadResult& other) noexcept {
            *this = operator=(other);
        }
        ThreadResult(ThreadResult&& other) noexcept {
            *this = operator=(other);
        }
        [[nodiscard]] std::future<Ry_> rawFutureResult() const {
            return std::move(m_future);
        }
        [[nodiscard]] std::size_t taskID() const {
            return m_TaskID;
        }
        auto get() {
            return m_future.get();
        }
        auto valid() {
            return m_future.valid();
        }
        auto wait() {
            return m_future.wait();
        }
        auto wait_for(const std::chrono::milliseconds& timeout) {
            return m_future.wait_for(timeout);
        }
    private:
        std::size_t m_TaskID{ 0 };
        std::future<Ry_> m_future;
    };

    /*
     * Thread Pool Manager
     */
    class ThreadPoolMgr final
    {
    public:
        /*
         * initialize threads pool max thread counts
         */
        bool initialize(const std::size_t maxThreadCounts = std::thread::hardware_concurrency()) {
            m_maxThreadCounts = maxThreadCounts;
            m_runnigSate = true;
            for (std::size_t i = 0; i < m_maxThreadCounts; ++i) {
                m_threads.emplace_back(std::thread([this] {
                    while (true) {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_cv.wait(lock, [this]()->bool {return !m_queueTasks.empty() || !m_runnigSate; });
                        if (!m_runnigSate) {
                            break;
                        }
                        const auto task = m_queueTasks.top();
                        m_queueTasks.pop();
                        if (!task) {
                            lock.unlock();
                            continue;
                        }
                         // remove map table element
                        const auto it = m_mapTasks.find(task->getTaskID());
                        if (it != m_mapTasks.end()) {
                            m_mapTasks.erase(it);
                        }
                        lock.unlock();
                        if (task && task->state() != AsyncTask::taskState::invalid) {
                            task->run();
                        }
                    }
                    }));
            }
            return true;
        }
        ~ThreadPoolMgr() {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_runnigSate = false;
                m_cv.notify_all();
            }
            for (auto& thread : m_threads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
            m_mapTasks.clear();
            while (!m_queueTasks.empty())
                m_queueTasks.pop();
        }
        /*
         * add tasks to threads
         */
        template<typename Fn, typename ...Args>
        auto addTask(Fn&& fn, Args&&... args) -> std::unique_ptr<ThreadResult<std::invoke_result_t<Fn, Args...>>> {
            using return_type = std::invoke_result_t<Fn, Args...>;
            std::unique_ptr <ThreadResult<return_type>> result{ nullptr };
            if (!m_runnigSate) return result;
            auto packagedTask = std::make_shared<std::packaged_task<return_type()> >(
                std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...)
                );
            const auto taskID = m_taskIDCounters.fetch_add(1);
            std::future<return_type> res = packagedTask->get_future();
            auto taskPtr = std::make_shared<AsyncTask>(taskID, [packagedTask]() {
                (*packagedTask)();
                });
            result = std::make_unique<ThreadResult<return_type>>(taskID, std::move(res));
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_mapTasks.emplace(taskID, taskPtr);
                m_queueTasks.emplace(taskPtr);
            }
            m_cv.notify_one();
            return result;
        }
        /*
         * raise Task  execute level
        */
        bool raiseTaskLevel(const std::size_t taskID) {
            std::unique_lock<std::mutex> lock(m_mutex);
            const auto task = m_mapTasks.find(taskID);
            if (task == m_mapTasks.end()) {
                return false;
            }
            task->second->addLevel(m_TaskLevelCounters.fetch_add(1));
            return true;
        }
        /*
         * remove task
         */
        bool removeTask(const std::size_t taskID) {
            std::unique_lock<std::mutex> lock(m_mutex);
            const auto task = m_mapTasks.find(taskID);
            if (task == m_mapTasks.end()) {
                return false;
            }
            if(task->second)
            {
                return task->second->abort();
            }
            return false;
        }
        /*
         * query task state
         */
        AsyncTask::taskState taskState(const std::size_t taskID)
        {
            const AsyncTask::taskState result{ AsyncTask::taskState::invalid };
            std::unique_lock<std::mutex> lock(m_mutex);
            const auto task = m_mapTasks.find(taskID);
            if (task == m_mapTasks.end()) {
                return result;
            }
            if (task->second) {
                return task->second->state();
            }
            return result;
        }
    private:
        std::mutex m_mutex;
        std::map<std::size_t, AsyncTask::Ptr> m_mapTasks;
        std::atomic<std::size_t> m_taskIDCounters{ 0 };
        std::condition_variable m_cv;
        std::size_t m_maxThreadCounts{ 0 };
        std::atomic<std::size_t> m_TaskLevelCounters{ 0 };
        std::priority_queue<AsyncTask::Ptr, std::vector<AsyncTask::Ptr>, TaskCompare> m_queueTasks;
        std::vector<std::thread> m_threads;
        std::atomic_bool m_runnigSate{ false };

    };
}
