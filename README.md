## A simple C++17 based thread pool implementation

Currently achieved

+ Add any function to the thread pool for execution
  Supports adding class member functions, imitation functions, lambda, and common functions
+ Support any type and any number of parameters
+ Support asynchronous fetching of thread pool execution results
+ Support for prioritized task execution
+ Support for aborting pending tasks

+ Supports creating a specified number of thread pools, the default is the number of threads for the number of logical CPUs



### Usage examples

```c++
#include <iostream>
#include "ThreadPool.hpp"
int main() {
    ThreadPool::ThreadPoolMgr ThreadMgr;
    // Initializing the thread pool
    ThreadMgr.initialize(4);
    //Create a vec that stores the returned results
    std::vector<std::unique_ptr<ThreadPool::ThreadResult<int>>> result_vec;
    //Adding tasks to the thread pool
    for (auto i = 0; i < 200; ++i) {
        auto result = ThreadMgr.addTask([](const int& a, const int& b)->int {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            return a + b;
            }, i, i + 1);
        //Raise the priority level of the current task
        ThreadMgr.raiseTaskLevel(result->taskID());
        result_vec.emplace_back(std::move(result));
    }
    // Get the results of asynchronous execution
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
```

