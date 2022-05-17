#include <iostream>
#include "ThreadPool.hpp"
int main() {
    ThreadPool::ThreadPoolMgr ThreadMgr;
    ThreadMgr.initialize(4);
    std::vector<std::unique_ptr<ThreadPool::ThreadResult<int>>> result_vec;
    for (auto i = 0; i < 200; ++i) {
        auto result = ThreadMgr.addTask([](const int& a, const int& b)->int {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            return a + b;
            }, i, i + 1);
        ThreadMgr.raiseTaskLevel(result->taskID());
        result_vec.emplace_back(std::move(result));
    }
    try {
        for (auto& v : result_vec) {
            std::cout << v->get() << std::endl;
        }
    }
    catch (std::exception& e) {
        std::cout << e.what() << std::endl;
    }
    system("pause");
    return 0;
}