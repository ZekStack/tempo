#include <Arduino.h>
#include <Tempo.h>

Tempo tempo;
TempoScheduler scheduler;

void setup() {
	Serial.begin(115200);

	TempoConfig tempoConfig;
	tempoConfig.timezone = "UTC0";
	tempoConfig.minValidUnixSeconds = 0;
	tempo.begin(tempoConfig);
	scheduler.begin(tempo);

	SchedulerJobOptions inlineOptions;
	inlineOptions.name = "inline";
	inlineOptions.mode = SchedulerJobMode::Inline;
	scheduler.schedule(TempoSchedule::everyMinutes(1), inlineOptions, []() {
		Serial.println("inline");
	});

	SchedulerJobOptions workerOptions;
	workerOptions.name = "worker";
	workerOptions.mode = SchedulerJobMode::WorkerPool;
	scheduler.schedule(TempoSchedule::everyMinutes(2), workerOptions, []() {
		Serial.println("worker pool");
	});

	SchedulerJobOptions dedicatedOptions;
	dedicatedOptions.name = "dedicated";
	dedicatedOptions.mode = SchedulerJobMode::DedicatedTask;
	scheduler.schedule(TempoSchedule::everyMinutes(3), dedicatedOptions, []() {
		Serial.println("dedicated task");
	});
}

void loop() {
	delay(1000);
}
