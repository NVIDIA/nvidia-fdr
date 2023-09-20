/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include <unistd.h>
#include <systemd/sd-bus.h>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "fdr.hpp"
#include "fdr_utils.hpp"

// TODO: message callback must move to use sdbusplus methods instead systemd sdbus
/*
int message_callback(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
	(void)userdata;
	(void)ret_error;

	printf("callback: path=%s interface=%s member=%s\n",
		   strna(sd_bus_message_get_path(m)),
		   strna(sd_bus_message_get_interface(m)),
		   strna(sd_bus_message_get_member(m)));

	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int r;

	r = sd_bus_get_property(bus, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager/Devices/1",
							"org.freedesktop.NetworkManager.Device.Statistics", "RxBytes",
							&error, &reply, "t");
	if (r < 0)
	{
		printf("sd_bus_get_property failed: error=%s\n", error.message);
	}

	uint64_t rxbytes;
	r = sd_bus_message_read(reply, "t", &rxbytes);
	if (r < 0)
		printf("sd_bus_message_read failed\n");

	printf("rxbytes =%" PRIu64 "\n", rxbytes);
	// sd_bus_message_dump(reply, stdout, SD_BUS_MESSAGE_DUMP_SUBTREE_ONLY);

	return 0;
}
*/

FlightDataRecorder_c *fdr;


static std::atomic<bool> exitSignal = false;

// get rid of -Wunused-parameter
void signalHandler(__attribute__((unused))int signum)
{
	exitSignal = true;
}

int main(int argc, char *argv[])
{
	// the fdr-init logger is used before fdr->log initialization
	auto console = spdlog::stdout_logger_mt("fdr-init");
	console->set_level(spdlog::level::info);
	spdlog::set_default_logger(console);

	// GOOGLE_PROTOBUF_VERIFY_VERSION;//Ensure protobuf header and library are compatible.

#ifndef FDR_TIMER_EVENT_ENABLED
	auto& bus = getBus();
#endif

	// a quick and dirty way to specify the platform definination file instead of detection
	std::string filename{};
	if (argc >= 2) {
		filename = argv[1];
	}
	fdr = new FlightDataRecorder_c(filename);
	if (fdr == nullptr) {
		spdlog::error("Error instantiating fdr instance");
		exit(1);
	}

// TODO: sdbus signals watch must move to use sdbusplus methods instead systemd sdbus
/*
#if 0
	// Install Listeners so we can avoid polling as much as possible
	sd_bus_match_signal(
		bus,												// bus
		NULL,												// ret
		NULL,												// sender
		"/org/freedesktop/NetworkManager/Devices/1",		// path
		"org.freedesktop.NetworkManager.Device.Statistics", // interface
		"PropertiesChanged",								// member
		message_callback,									// callback
		NULL);												// userdata

	while (1)
	{
		sd_bus_wait(bus, UINT64_MAX);
		while (sd_bus_process(bus, NULL))
		{
		}
	}
#endif
*/
	// create the birth certificate archive file, if needed
	fdr->CollectAndArchieveBirthCertificate();

	// Register AML events signal
	fdr->initEventsSignalRegistration();


#ifdef FDR_TIMER_EVENT_ENABLED
	fdr->InitTimerEvents();
	fdr->RunEventLoop();
#else
	// TODO: consider adding SIGTERM for systemd stop
	signal(SIGINT, signalHandler);

	while (true)
	{
		if (exitSignal) {
			spdlog::info("Received received, exiting!");
			// TODO: some clean up?
			exit(0);
		}

		/*
		   Note: the try catch block is mainly for fdr->Compactor();
		   fdr->RefreshAndStore() internally is a simple loop with similar try/catch block,
	       so that if a record failed, it will proceed to next item.

		   However, fdr->CheckExceptionRateLimit() is more complicated, 
		   this try/catch block is added here for simplicity.
		*/
		bool expt = false;
		try {
			// Start the core engine of fetching and recording
			fdr->RefreshAndStore(false);

			// Compactor
			fdr->Compactor(false);

			// Process the waiting dbus messages or signals
			bus.process_discard();

		} catch (const std::exception& e) {
			expt = true;
			fdr->log->warn("RefreshAndStore Got Exception: {}", e.what());
		} catch (...) {
			expt = true;
			fdr->log->warn("RefreshAndStore Got Unknown Exception !!!");
		}

		if (expt) {
			fdr->CheckExceptionRateLimit();
		}

		sleep(1);
	}

	bus.close(); // Close the connection to the dbus
#endif

	return EXIT_SUCCESS;
}

#if 0
std::cout << __func__ << ":" << __LINE__ << std::endl;
#endif
