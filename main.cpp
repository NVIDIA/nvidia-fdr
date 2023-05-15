
#include <unistd.h>
#include <systemd/sd-bus.h>

#include "fdr.hpp"

static inline const char *strna(const char *s)
{
	return s ? s : "n/a";
}

sd_bus *bus = NULL;

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

FlightDataRecorder_c *fdr;

int main(int argc, char *argv[])
{
	// GOOGLE_PROTOBUF_VERIFY_VERSION;//Ensure protobuf header and library are compatible.

	sd_bus_default_system(&bus);

	// a quick and dirty way to specify the platform definination file instead of detection
	std::string filename{};
	if (argc >= 2) {
		filename = argv[1];
	}
	fdr = new FlightDataRecorder_c(filename);

	// Read in the last recorded values from log files
	fdr->ReadOldRecords();

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

	// create the birth certificate archive file, if needed
	fdr->CollectAndArchieveBirthCertificate();

	while (true)
	{
		// Start the core engine of fetching and recording
		fdr->RefreshAndRecord(true);

		// Compactor
		fdr->Compactor();

		sleep(1);
	}

	sd_bus_unref(bus);
	return EXIT_SUCCESS;
}

#if 0
std::cout << __func__ << ":" << __LINE__ << std::endl;
#endif
