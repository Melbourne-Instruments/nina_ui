/**
 *-----------------------------------------------------------------------------
 * Copyright (c) 2021-2023 Melbourne Instruments, Australia
 *-----------------------------------------------------------------------------
 * @file  timer.cpp
 * @brief Timer implementation.
 *-----------------------------------------------------------------------------
 */

#include <stdint.h>
#include <cstring>
#include <iostream>
#include "timer.h"
#include "common.h"


//----------------------------------------------------------------------------
// Timer
//----------------------------------------------------------------------------
Timer::Timer(TimerType type)
{
	// Initialise the private data
	_timer_type = type;
	_timer_running = false;
	_timer_signalled = false;
	_timer_thread = 0;
	_interval_us = 0;
	_callback_fn = 0;
}

//----------------------------------------------------------------------------
// ~Timer
//----------------------------------------------------------------------------
Timer::~Timer()
{
	// Stop the timer if running
	if (_timer_running)
    	stop();
}

//----------------------------------------------------------------------------
// start
//----------------------------------------------------------------------------
void Timer::start(int interval_us, std::function<void(void)> callback_fn)
{
	// Check if a timer thread already exists - we must always ensure
	// it is stopped and deallocated before starting a new timer
	if (_timer_thread) {
    	stop();
	}

	// Set the interval and callback function
	_interval_us = interval_us;
	_callback_fn = callback_fn;

	// Indicate the timer is now running
	_timer_running = true;
	_timer_signalled = false;

	// Start the timer thread
	_timer_thread = new std::thread(&Timer::_timer_callback, this);
}

//----------------------------------------------------------------------------
// signal
//----------------------------------------------------------------------------
void Timer::signal()
{
	// Signal the timer
	_timer_signalled = true;
	_cv.notify_all();
}

//----------------------------------------------------------------------------
// change_interval
//----------------------------------------------------------------------------
void Timer::change_interval(int interval_us)
{
	// Get the mutex and change the timer interval
	_interval_us = interval_us;
}

//----------------------------------------------------------------------------
// stop
//----------------------------------------------------------------------------
void Timer::stop()
{
	// Stop the timer
	{
		std::lock_guard<std::mutex> lk(_mutex);
		_timer_running = false;
		_timer_signalled = true;
	}
	_cv.notify_all();
	
	// Stop the timer thread
	if (_timer_thread)
	{
		// Wait for the timer thread to finish and delete it
		if (_timer_thread->joinable())
			_timer_thread->join();
		delete _timer_thread;
		_timer_thread = 0;
	}
}

//----------------------------------------------------------------------------
// is_running
//----------------------------------------------------------------------------
bool Timer::is_running()
{
    return (_timer_thread && _timer_running);
}

//----------------------------------------------------------------------------
// _timer_callback
//----------------------------------------------------------------------------
void Timer::_timer_callback()
{
	std::chrono::duration<double> adj = std::chrono::microseconds(0);

	// Do forever until stopped
	while (true)
	{
		// Get the mutex lock
		std::unique_lock<std::mutex> lk(_mutex);

		// Calculate the wait duration
		auto const timeout = std::chrono::steady_clock::now() + std::chrono::microseconds(_interval_us) - adj;

		// Wait for either a timeout or for the timer to be signalled
		bool res = _cv.wait_until(lk, timeout, [this]{return _timer_signalled;});

		// Did either a timeout occur, or the timer signalled and still running?
		if (!res || _timer_running)
		{
			// Save the time at the start of processing
			std::chrono::_V2::system_clock::time_point start = std::chrono::high_resolution_clock::now();

			// Call the callback function
			(_callback_fn)();

			// Is this a one-shot timer? If so, stop the timer task
			if (_timer_type == TimerType::ONE_SHOT)
				break;
			
			// Reset the timer signalled indicator
			_timer_signalled = false;

			// Calculate the processing time adjustment
			adj = std::chrono::high_resolution_clock::now() - start;			
		}
		else 
		{
			// A timeout did not occur, meaning the timer was signalled and is
			// no longer running
			// In this case we can break from the thread loop
			break;
		}
	}

	// Indicate the timer is no longer running
	_timer_running = false;
}
