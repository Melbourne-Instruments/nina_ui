/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  timer.h
 * @brief Timer class definitions.
 *-----------------------------------------------------------------------------
 */
#ifndef _TIMER_H
#define _TIMER_H

#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

// Timer Type
enum class TimerType
{
	ONE_SHOT,
	PERIODIC
};

// Timer class
class Timer
{
public:
    // Constructor
    Timer(TimerType type);

    // Destructor
    virtual ~Timer();

    // Public functions
    void start(int interval_us, std::function<void(void)> callback_fn);
    void signal();
    void change_interval(int interval_us);
    void stop();
    bool is_running();

private:
    // Private data
    TimerType _timer_type;
    std::mutex _mutex;
    std::condition_variable _cv;
    bool _timer_running;
    bool _timer_signalled;
    std::thread *_timer_thread;
    int _interval_us;
    std::function<void(void)> _callback_fn;
    
    // Private functions
    void _timer_callback();
};

#endif  // _TIMER_H
