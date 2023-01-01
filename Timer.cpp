#include <windows.h>
#include "Timer.h"

Timer::Timer()
{
	__int64 countsPerSec{};
	QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&countsPerSec));
	_secondsPerCount = 1.0 / static_cast<double>(countsPerSec);
}

float Timer::TotalTime() const
{
	if (_stopped)
	{
		return static_cast<float>((((_stopTime - _pausedTime) - _baseTime) * _secondsPerCount));
	}
	else
	{
		return static_cast<float>((((_currTime - _pausedTime) - _baseTime) * _secondsPerCount));
	}
}

void Timer::Reset()
{
	__int64 currTime{};
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currTime));

	_baseTime = currTime;
	_prevTime = currTime;
	_stopTime = 0;
	_stopped = false;
}

void Timer::Start()
{
	__int64 startTime{};
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&startTime));

	if (_stopped)
	{
		_pausedTime += (startTime - _stopTime);

		_prevTime = startTime;
		_stopTime = 0;
		_stopped = false;
	}
}

void Timer::Stop()
{
	if (!_stopped)
	{
		__int64 currTime{};
		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currTime));

		_stopTime = currTime;
		_stopped = true;
	}
}

void Timer::Tick()
{
	if (_stopped)
	{
		_deltaTime = 0.0;
		return;
	}

	__int64 currTime{};
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currTime));
	_currTime = currTime;

	_deltaTime = (_currTime - _prevTime) * _secondsPerCount;

	_prevTime = _currTime;

	if (_deltaTime < 0.0)
	{
		_deltaTime = 0.0;
	}
}