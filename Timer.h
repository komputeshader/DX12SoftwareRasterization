#pragma once

class Timer
{
public:
	Timer();

	// in seconds
	float TotalTime() const;

	// in seconds
	float DeltaTime() const { return static_cast<float>(_deltaTime); }

	// call before message loop
	void Reset();

	// call when unpaused
	void Start();

	// call when paused
	void Stop();

	// call every frame
	void Tick();

private:

	double _secondsPerCount = 0.0;
	double _deltaTime = -1.0;

	__int64 _baseTime = 0;
	__int64 _pausedTime = 0;
	__int64 _stopTime = 0;
	__int64 _prevTime = 0;
	__int64 _currTime = 0;

	bool _stopped = false;
};