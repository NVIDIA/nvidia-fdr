/*
 Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

#include "fdr.hpp"
#include "fdr_log.hpp"
#include "fdr_utils.hpp"

#include <systemd/sd-bus.h>
#include <unistd.h>

FlightDataRecorder_c* fdr;

static std::atomic<bool> exitSignal = false;

// get rid of -Wunused-parameter
void signalHandler(__attribute__((unused)) int signum)
{
    exitSignal = true;
}

int main(int argc, char* argv[])
{
#ifdef FDR_USE_SPDLOG
    fdrlog::debug("Using spdlog");
#else
    fdrlog::debug("using phosphor-logging");
#endif

    // GOOGLE_PROTOBUF_VERIFY_VERSION;//Ensure protobuf header and library are
    // compatible.

#ifndef FDR_TIMER_EVENT_ENABLED
    auto& bus = getBus();
#endif

    // a quick and dirty way to specify the platform definination file instead
    // of detection
    std::string filename{};
    if (argc >= 2)
    {
        filename = argv[1];
    }
    fdr = new FlightDataRecorder_c(filename);
    if (fdr == nullptr)
    {
        fdrlog::error("Error instantiating fdr instance");
        exit(1);
    }

    // create the birth certificate archive file, if needed
    fdr->CollectAndArchieveBirthCertificate();

    // Register AML events signal
    fdr->initEventsSignalRegistration();

    // Register property changed DBUS signal
    fdr->initRecordsSignalRegistration();

#ifdef FDR_TIMER_EVENT_ENABLED
    constexpr auto FDR_BUSNAME = "xyz.openbmc_project.FDR";
    try
    {
        sd_bus* fdrBus = nullptr;
        auto rc = sd_bus_default_system(&fdrBus);
        if (rc < 0)
        {
            fdrlog::error("Exiting, Failed to connect to system bus: rc: {}",
                          rc);
            return EXIT_FAILURE;
        }
        auto io = std::make_shared<boost::asio::io_context>();
        auto sdbusp = std::make_shared<sdbusplus::asio::connection>(*io,
                                                                    fdrBus);
        sdbusp->request_name(FDR_BUSNAME);
        fdr->InitTimerEvents();
        fdr->RunEventLoop();
    }
    catch (const std::exception& e)
    {
        fdrlog::error("FDR init error: {}", e.what());
        return EXIT_FAILURE;
    }
#else
    // TODO: consider adding SIGTERM for systemd stop
    signal(SIGINT, signalHandler);

    while (true)
    {
        if (exitSignal)
        {
            fdrlog::info("Received received, exiting!");
            // TODO: some clean up?
            exit(0);
        }

        /*
           Note: the try catch block is mainly for fdr->Compactor();
           fdr->RefreshAndStore() internally is a simple loop with similar
           try/catch block, so that if a record failed, it will proceed to next
           item.

           However, fdr->CheckExceptionRateLimit() is more complicated,
           this try/catch block is added here for simplicity.
        */
        bool expt = false;
        try
        {
            // Start the core engine of fetching and recording
            fdr->RefreshAndStore(false);

            // Compactor
            fdr->Compactor(false);

            // Process the waiting dbus messages or signals
            bus.process_discard();
        }
        catch (const std::exception& e)
        {
            expt = true;
            fdrlog::warn("RefreshAndStore Got Exception: {}", e.what());
        }
        catch (...)
        {
            expt = true;
            fdrlog::warn("RefreshAndStore Got Unknown Exception !!!");
        }

        if (expt)
        {
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
