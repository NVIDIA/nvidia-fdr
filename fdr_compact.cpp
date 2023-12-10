/*
 Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.

 NVIDIA CORPORATION and its licensors retain all intellectual property
 and proprietary rights in and to this software, related documentation
 and any modifications thereto.  Any use, reproduction, disclosure or
 distribution of this software and related documentation without an express
 license agreement from NVIDIA CORPORATION is strictly prohibited.
*
*/

// This file contain definitions for all the methods related to compaction in FDR

#include "fdr.hpp"
#include "fdr_log.hpp"
#include <filesystem>

// This method will slide through BookOfError.dat file and find any error occured between the given 2 timestamps.
// If yes, then get the list of those errors between those 2 timestamps.
bool FlightDataRecorder_c::CompactorCheckBookOfErrors(const std::string directoryTocompact,
											 uint64_t leastWindowTimestamp,
											 uint64_t farWindowTimestamp,
											 std::vector<fdrpb::fdr_book_of_errors> &errorList)
{
	std::string bookOfErrorsFileName = profile.GeneralConfig.LogsBasePath + "/" + 
									   CommonFdrKeepersDirName + "/" +
									   BookOfErrorKeeperName;

	// check if the file exists
	if (!(std::filesystem::exists(bookOfErrorsFileName))) {
		fdrlog::warn("Book of errors file Not Exist!!: {}", bookOfErrorsFileName);
		return false;
	}

	auto splittedList = split(directoryTocompact, '_');
	if (splittedList.size() != 4) {
		// should never happen at this point..but checking for code completeness.
		fdrlog::warn("not a valid directoryTocompact: {}"
					   "; splittedList.size(): {}",
					   directoryTocompact,
					   splittedList.size());
		return false;
	}

	std::string givenBootId = splittedList[1];
	fdrlog::debug("CompactorCheckBookOfErrors: givenBootId: {}", givenBootId);

	// loop through the BookOfError.dat file and check if any error occured during this given time period
	fdrpb::fdr_book_of_errors errorRecord;
	FDRStore BookOfErrorReader(bookOfErrorsFileName, profile.GeneralConfig.LogsFormat, STORE_READER);
	while (BookOfErrorReader.readnext(&errorRecord)) {
		// std::cout << "errorRecord: erroroccurtimeStamp: " << errorRecord.erroroccurtimestamp()
		// 		  << "; BootId: " << errorRecord.bootid()
		// 		  << "; ParamID: " << errorRecord.paramid()
		// 		  << "; DeviceInstance: " << errorRecord.deviceinstance()
		// 		  << "; ErrorType: " << errorRecord.errortype()
		// 		  << "; givenBootId: " << givenBootId
		// 		  << std::endl;
		if (givenBootId == errorRecord.bootid()) {
			uint64_t adjustedLeastWindowTimestamp = leastWindowTimestamp - profile.GeneralConfig.HiFiDataPreserveTimeSecs;
			uint64_t adjustedFarWindowTimestamp = farWindowTimestamp + profile.GeneralConfig.HiFiDataPreserveTimeSecs;
			// std::cout << "leastWindowTimestamp: " << leastWindowTimestamp
			// 		  << "; farWindowTimestamp: " << farWindowTimestamp
			// 		  << "; adjustedLeastWindowTimestamp: " << adjustedLeastWindowTimestamp
			// 		  << "; adjustedFarWindowTimestamp: " << adjustedFarWindowTimestamp
			// 		  << std::endl;

			// This adjustedLeastWindowTimestamp is to cover the error event present on the previous DirectoryToCompact
			// This adjustedFarWindowTimestamp is to cover the error event present on the next DirectoryToCompact
			if (CheckIsInRange(adjustedLeastWindowTimestamp, adjustedFarWindowTimestamp, errorRecord.erroroccurtimestamp())) {
				// this is very important
				errorRecord.set_starthifitimestamp(errorRecord.erroroccurtimestamp()-profile.GeneralConfig.HiFiDataPreserveTimeSecs);
				errorRecord.set_stophifitimestamp(errorRecord.erroroccurtimestamp()+profile.GeneralConfig.HiFiDataPreserveTimeSecs);

				// append this error record to the return list
				errorList.push_back(errorRecord);
			}
		}
	}

	// for debugging
	// for (auto &errorRecord : errorList) {
	// 	std::cout << "----erroroccurtimestamp: " << errorRecord.erroroccurtimestamp()
	// 			  << "; starthifitimestamp: " << errorRecord.starthifitimestamp()
	// 			  << "; stophifitimestamp: " << errorRecord.stophifitimestamp()
	// 			  << "; BootId: " << errorRecord.bootid()
	// 			  << "; SectionId: " << errorRecord.compclass()
	// 			  << "; ParamClass: " << errorRecord.paramclass()
	// 			  << "; ParamID: " << errorRecord.paramid()
	// 			  << "; DeviceInstance: " << errorRecord.deviceinstance()
	// 			  << "; ErrorType: " << errorRecord.errortype()
	// 			  << "; givenBootId: " << givenBootId
	// 			  << std::endl;
	// }
	return errorList.size()? true: false;
}

void FlightDataRecorder_c::Compactor(bool viaTimerSkipChecks)
{
	// pass the same time to both sub window expiracy and main window expiracy checks,
	// [as there could be slight timing issues]
	std::time_t current_time = std::time(nullptr);

	// 1st check for main window compaction time expiry
	auto mainWindowCompactStatus = CheckCompactionWindowExpiry(viaTimerSkipChecks, current_time);

	// 2nd, if main window compaction time expired, then we need to append the 
	// last few collected stats to the stat file
	CheckCompactionSubWindowExpiry(viaTimerSkipChecks, current_time, mainWindowCompactStatus);

	// 3rd, if main window compaction time expired, then we need to recreate the records
	CompactionWindowExpiryCleanUp(mainWindowCompactStatus);
}

// This method takes appropriate actions on every compaction window expiry
int FlightDataRecorder_c::CheckCompactionWindowExpiry(bool viaTimerSkipChecks, std::time_t current_time)
{
	double timeSinceLastCompactionWindowDirCreation = difftime(current_time, LastCompactWindowDirCreationSecsAt);

	fdrlog::debug("timeSinceLastCompactionWindowDirCreation: {}"
	           "; LastCompactWindowDirCreationSecsAt: {}"
			   "; CompactionWindowSecs: {}",
			   timeSinceLastCompactionWindowDirCreation,
			   LastCompactWindowDirCreationSecsAt,
			   profile.GeneralConfig.CompactionWindowSecs);

	// Skip if its not time to compact yet
	if (viaTimerSkipChecks == true ||
		timeSinceLastCompactionWindowDirCreation >= profile.GeneralConfig.CompactionWindowSecs) {
		// check and exit fdr if disk availability is less.
		CheckFdrPartitionDiskUsageAndExit();

		fdrlog::debug("======================creating new directory for sensors======================");
		// check for any compaction needs to be done and get the name of the directory to compact
		auto directoryTocompact = CompactorGetDirectoryToCompact(RUN_TIME_DIR_COUNT);
		if (directoryTocompact.empty()) {
			fdrlog::debug("Nothing to compact..Empty directoryTocompact!!");
		} else {
			fdrlog::debug("directoryTocompact: {}", directoryTocompact);

            // call the compactor engine which does the rest of the compaction job
			CompactorEngine(directoryTocompact);
		}

		// reset to the current time
		LastCompactWindowDirCreationSecsAt = std::time(nullptr);

		return FDR_SUCCESS;
	}

	return FDR_ERR_MAIN_WINDOW_NOT_EXPIRED;
}

// cleanup action when main window compaction time got expired:
// * need to recreate the records
// * update the global variables
// * update the Compactor BookKeeper
void FlightDataRecorder_c::CompactionWindowExpiryCleanUp(int mainWindowCompactStatus)
{
	if (mainWindowCompactStatus != FDR_SUCCESS) {
		return;
	}
	// update the global variable bootCounter and sensorDirTimestamp
	UpdateGlobVariables(false);

	// std::cout << "before modifying-------------------------------------------------------------------------" << std::endl;
	// std::cout << "size of RecList:" << RecList.size() << std::endl;
	// for (auto recIt =  RecList.begin(); recIt !=  RecList.end(); ++recIt) { 
	// 	(*recIt)->Print();
	// } 
	ModifySpecificRecords("Recreate");

	// std::cout << "after modifying-------------------------------------------------------------------------" << std::endl;
	// std::cout << "size of RecList:" << RecList.size() << std::endl;
	// for (auto recIt =  RecList.begin(); recIt !=  RecList.end(); ++recIt) { 
	// 	(*recIt)->Print();
	// } 

	// after creating the specific records again, update the Compactor BookKeeper
	CompactorBookKeeperAppendEntry();
}

// This method takes appropriate actions on every compaction window expiry
int FlightDataRecorder_c::CheckCompactionSubWindowExpiry(bool viaTimerSkipChecks, std::time_t current_time, int mainWindowCompactStatus)
{
	double timeSinceLastCompactionSubWindowDirCreation = difftime(current_time, LastCompactSubWindowDirCreationSecsAt);

	fdrlog::debug("timeSinceLastCompactionSubWindowDirCreation: {}"
	           "; LastCompactSubWindowDirCreationSecsAt: {}"
			   "; CompactionSubWindowSecs: {}"
			   "; mainWindowCompactStatus: {}",
			   timeSinceLastCompactionSubWindowDirCreation,
			   LastCompactSubWindowDirCreationSecsAt,
			   profile.GeneralConfig.CompactionSubWindowSecs,
			   mainWindowCompactStatus);

	// check if sub window compaction time got expired [time to append the stat file yet] 
	// or main window compaction time got expired[in this case, we need to append the last few pending 
	// stat collected. scenario: main window compaction time: say 150, sub window compaction time: say 40.
	// in this case, that last 30 seconds stats needs to be appended to the stat file]
	if (viaTimerSkipChecks == true ||
		timeSinceLastCompactionSubWindowDirCreation >= profile.GeneralConfig.CompactionSubWindowSecs ||
		mainWindowCompactStatus == FDR_SUCCESS) {
		fdrlog::debug("======================sub window timer expired======================");

		// check and exit fdr if disk availability is less.
		CheckFdrPartitionDiskUsageAndExit();

		for (auto &rec : RecList) {
			if (rec->infogroup.CompactionMethod != "Average") {
				continue;
			}
			rec->appendRunningStatToStatfile();

		    // after appending, reset all the variables related to stat
			rec->ResetRunningStat();
		}
		// reset to the current time
		LastCompactSubWindowDirCreationSecsAt = std::time(nullptr);
		return FDR_SUCCESS;
	}
	return FDR_ERR_SUB_WINDOW_NOT_EXPIRED;
}

// This method will get the least timestamp and the far timestamp from the compact window directory name itself.
// [which will latter updated into their corresponding entry compactor book keeper file by the caller]
void FlightDataRecorder_c::CompactorGetLeastAndFarTimestamp(const std::string directoryTocompact,
									uint64_t &leastWindowTimestamp, uint64_t &farWindowTimestamp)
{
	// reset the variable references
	leastWindowTimestamp = 0;
	farWindowTimestamp = 0;
    char* endPtr;  // Pointer to the character after the converted number

	auto splittedList = split(directoryTocompact, '_');
	if (splittedList.size() != 4) {
		// should never happen at this point..but checking for code completeness.
		fdrlog::warn("not a valid directoryTocompact: {}"
					   "; splittedList.size(): {}",
					   directoryTocompact,
					   splittedList.size());
	} else {
		fdrlog::debug("CompactorGetLeastAndFarTimestamp: directory timestamp: {}", splittedList[3]);
		leastWindowTimestamp = std::strtoull(splittedList[3].c_str(), &endPtr, 10);
		if (*endPtr == '\0') {
			// std::cout << "Converted value: " << leastWindowTimestamp << std::endl;
			farWindowTimestamp = leastWindowTimestamp + profile.GeneralConfig.CompactionWindowSecs;
		} else {
			fdrlog::warn("Error converting string: {}", splittedList[3]);
		}
	}
	// std::cout << "2. finally: leastWindowTimestamp: " << leastWindowTimestamp
	// 			<< "; farWindowTimestamp: " << farWindowTimestamp << std::endl;
}

// This method will scans through the samples log files and check if the record falls in any
// of the errorlist range. If yes, then collect the High Fidelity logs and writes the
// collected logs into a separate *.hifilog file.
void FlightDataRecorder_c::CompactorCreateHighFidelityFiles(const std::string directoryTocompact,
									std::vector<fdrpb::fdr_book_of_errors> errorList)
{
    namespace fs = std::filesystem;

    // Set the directory path
    fs::path directoryPath = profile.GeneralConfig.LogsBasePath + "/" + directoryTocompact + "/";

    // Iterate over the files in the directory
    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        // Check if the file name ends with ".sensors.dat"
        if (entry.path().filename().string().ends_with(".sensors.dat")) {
			// get the source and destination file name
			std::string sourceLogfile = entry.path();
			std::string destHighFidelityLogile = entry.path();
			FindAndReplaceAll(destHighFidelityLogile, ".sensors.dat", ".sensors.hifi.dat");

			// collect the high fidelity logs for every instance of its components and
			// write it immdiately and clear the variable
			std::vector<fdrpb::fdr_sample> hifiRecords;

			// 1. read every record from the *.sensors.dat file
			fdrpb::fdr_sample readrec;
			FDRStore fdrLogSamplesReader(sourceLogfile, profile.GeneralConfig.LogsFormat, STORE_READER);
			while (fdrLogSamplesReader.readnext(&readrec)) {
				// loop through the errorList and find if this record is falling in any of the hifi range
				for (auto &errorRecord : errorList) {
					// std::cout << "erroroccurtimestamp: " << errorRecord.erroroccurtimestamp()
					// 		  << "; starthifitimestamp: " << errorRecord.starthifitimestamp()
					// 		  << "; stophifitimestamp: " << errorRecord.stophifitimestamp()
					// 		  << "; readrec.timestamp: " << readrec.timestamp()
					// 		  << std::endl;
					if (CheckIsInRange(errorRecord.starthifitimestamp(), errorRecord.stophifitimestamp(), readrec.timestamp())) {
						// if the current log is in range of any of the error list, then this log needs to be collected for hifi log.
						hifiRecords.push_back(readrec);
						break;
					}
				}
			}

			// 2. this loop will write the collected data into their respective directories
			FDRStore fdrHifiWriter(destHighFidelityLogile, profile.GeneralConfig.LogsFormat, STORE_WRITER);
			// loop through the hifiRecords and put them into the hifi log file
			for (auto &hifiRecord : hifiRecords) {
				fdrHifiWriter.append(hifiRecord);
			}
        }
    }
}

// This method will remove the *.dat file after processing them for any errors and 
// collecting the hifi logs if any errors found.
void FlightDataRecorder_c::CompactorRemoveSamplesLogfiles(std::string directoryTocompact)
{
    namespace fs = std::filesystem;

    // Set the directory path
    fs::path directoryPath = profile.GeneralConfig.LogsBasePath + "/" + directoryTocompact + "/";

    // Iterate over the files in the directory
    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        // Check if the file name ends with "sensors.dat"
        if (entry.path().filename().string().ends_with(".sensors.dat")) {
            // Delete the file
            fs::remove(entry.path());
            // std::cout << "Deleted file: " << entry.path() << std::endl;
        }
    }
}

// This method will append to the BookKeepers/Compactor.dat file with the new directory to be
// compacted in future[after the expiry of CompactionWindowSecs]
void FlightDataRecorder_c::CompactorBookKeeperAppendEntry(void) {
	fdrpb::fdr_compactor_bookkeep newEntry;
	newEntry.set_compactdirectory(GetDirectoryName());
	CompactorBookKeeperAppender->append(newEntry);
}

// This methos will remove a specified entry from BookKeepers/Compactor.dat
void FlightDataRecorder_c::CompactorBookKeeperRemoveEntry(std::string directoryTocompact)
{
	std::string logsformat = profile.GeneralConfig.LogsFormat;
    std::string logdir = profile.GeneralConfig.LogsBasePath + "/" + CommonFdrKeepersDirName + "/";
    std::string logfile = CompactorBookKeeperName;

	std::string logfilepath;
	logfilepath = logdir + logfile;

	// vectorize all the current entires from compactorBookKeeper.dat file
	fdrpb::fdr_compactor_bookkeep compactorBookKeeperRecord;
	std::vector<fdrpb::fdr_compactor_bookkeep> vectorizedcompactorBookKeeper;
	FDRStore CompactorBookKeeperReader(logfilepath, profile.GeneralConfig.LogsFormat, STORE_READER);
	while (CompactorBookKeeperReader.readnext(&compactorBookKeeperRecord)) {
		vectorizedcompactorBookKeeper.push_back(compactorBookKeeperRecord);
	}

	// std::cout << "b4: length of CompactorBookKeeperName: " 
	// 		  << vectorizedcompactorBookKeeper.size()
	// 		  << std::endl;

	// update the matching entries
	while (!vectorizedcompactorBookKeeper.empty()) {
		// std::cout << "topmost front directory: " 
		// 		  << vectorizedcompactorBookKeeper.front().compactdirectory()
		// 		  << std::endl;
		
		// if the entry matches
		if (vectorizedcompactorBookKeeper.front().compactdirectory() == directoryTocompact) {
			// delete the entry and break the loop
			vectorizedcompactorBookKeeper.erase(vectorizedcompactorBookKeeper.begin());
			break;
		} else {
			// remove all the unwanted/stale entires, if exist
			vectorizedcompactorBookKeeper.erase(vectorizedcompactorBookKeeper.begin());
		}
	}
	// std::cout << "after: length of CompactorBookKeeperName: " 
	// 		  << vectorizedcompactorBookKeeper.size()
	// 		  << std::endl;

    // check if there are any entiries 
    if (vectorizedcompactorBookKeeper.size() != 0) {
        // then write the updated vectorized entires to the newCompactor.dat file
        // [its first written to newCompactor.dat and then copied to Compactor.dat just to avoid the 
        // possible HMC reset while directly modifying Compactor.dat and further inconsistency]
        std::string compactorNewBookKeeper = logdir + "new" + logfile;
        // 1. remove the new file if already exist
        if (remove(compactorNewBookKeeper.c_str()) == 0) {
            fdrlog::debug("Successfully deleted if any stray compactorNewBookKeeper: {}", 
                    	compactorNewBookKeeper);
        }

        // 2. write to the newCompactor.dat
    	FDRStore fdrcompactorNewBookKeeperWriter(compactorNewBookKeeper, profile.GeneralConfig.LogsFormat, STORE_WRITER);
        for (auto &updatedBookofErrorRecord : vectorizedcompactorBookKeeper) {
            fdrcompactorNewBookKeeperWriter.append(updatedBookofErrorRecord);
        }

        // 3. copy newCompactor.dat to Compactor.dat
        std::string copyCommandStr = "cp " + compactorNewBookKeeper + " " + logfilepath;
        fdrlog::debug("copyCommandStr: {}", copyCommandStr);
        CommandResult_t cmdResult = exec(copyCommandStr.c_str());
        if (cmdResult.cmdExitstatus != FDR_SUCCESS) {
            fdrlog::warn("copy command Failed: {}", copyCommandStr);
            return;
        }
        fdrlog::debug("Successfully copied the new to current compactorBookKeeper file: {}", logfilepath);

        // 4. then remove the newCompactor.dat
        if (remove(compactorNewBookKeeper.c_str()) == 0) {
            fdrlog::debug("Successfully deleted compactorNewBookKeeper: {}", compactorNewBookKeeper);
        }
    } else {
        fdrlog::debug("length of CompactorBookKeeperName: 0..so simply remove the file");
        // if no entries, then simply remove the Compactor.dat
        if (remove(logfilepath.c_str()) == 0) {
            fdrlog::debug("Successfully deleted Compactor.dat: {}", logfilepath);
        }
    }
}

// Scroll through the Bookkeeper/BootEvent.dat file which have list of boot events along with
// the timestamp and data format during that time; and get that particular data format where
// this directoryTocompact lies.
uint32_t FlightDataRecorder_c::CompactorGetItsDataFormat(std::string directoryTocompact) {
	uint32_t oldDirsDataFormat = 0;

	std::string compactorFileName = profile.GeneralConfig.LogsBasePath + "/" + 
									   CommonFdrKeepersDirName + "/" +
									   BootEventKeeperName;

	// check if the file exists
	if (!(std::filesystem::exists(compactorFileName))) {
		fdrlog::warn("compactor Bookkeeper file Not Exist!!: {}", compactorFileName);
		return oldDirsDataFormat;
	}

	auto splittedList = split(directoryTocompact, '_');
	if (splittedList.size() != 4) {
		// should never happen at this point..but checking for code completeness.
		fdrlog::warn("not a valid directoryTocompact: {}"
					   "; splittedList.size(): {}",
					   directoryTocompact,
					   splittedList.size());
		return oldDirsDataFormat;
	}

	std::string oldDirBootId = splittedList[1];
	std::string errStr = "";
	uint64_t oldDirTimestamp = convertStrToUint64(splittedList[3], errStr);
	if (errStr != "") {
		fdrlog::error("CompactorGetItsDataFormat: error in converting: {}; error: {}", oldDirTimestamp, errStr);
		return oldDirsDataFormat;
	}
	
	// loop through the BootEvent.dat file and check if the directory falls during this given time period
	fdrpb::fdr_boot_event bootEventRecord;
	FDRStore CompactorFileReader(compactorFileName, profile.GeneralConfig.LogsFormat, STORE_READER);
	while (CompactorFileReader.readnext(&bootEventRecord)) {
		fdrlog::debug("1. CompactorGetItsDataFormat: EventTimeStamp: {}; BootId: {}; DataDirFormatVersion: {}", 
					bootEventRecord.eventtimestamp(), bootEventRecord.bootid(), bootEventRecord.datadirformatversion());
		if (oldDirBootId == bootEventRecord.bootid() &&
			oldDirTimestamp >= bootEventRecord.eventtimestamp()) {
			oldDirsDataFormat = bootEventRecord.datadirformatversion();
			fdrlog::debug("2. CompactorGetItsDataFormat: oldDirsDataFormat: {}", oldDirsDataFormat);
		}
	}

	fdrlog::debug("CompactorGetItsDataFormat: directoryTocompact: {}; oldDirBootId: {}; oldDirTimestamp: {}; oldDirsDataFormat: {}",
		directoryTocompact, oldDirBootId, oldDirTimestamp, oldDirsDataFormat);

	return oldDirsDataFormat;
}

// This method should be called only once when FDR starts.
// This method will scroll through the Compactor.dat file for any entries which are not yet compacted
// in the previous FDR instance. It will get those entries one by one and compact each directories
// [like creating .stat files and hifilogs[if any error]] for those old entiries.
// Number of entries in Compactor.dat will be 0 before this method returns.
void FlightDataRecorder_c::CompactorBookKeeperCleanEntries(void) {
    // loop for those many number of times as number of entries in the compactor bookkeeper log file
    for (;;) {
        // check for any compaction needs to be done and get the name of the directory to compact
        auto directoryTocompact = CompactorGetDirectoryToCompact(BOOT_TIME_DIR_COUNT);
        fdrlog::debug("------------BOOT_TIME: 1. directoryTocompact: {}", directoryTocompact);
        if (directoryTocompact.empty()) {
			fdrlog::debug("------------BOOT_TIME: Nothing to compact..Empty directoryTocompact!!------------");
            break;
        } else {
            fdrlog::debug("------------BOOT_TIME: 2. directoryTocompact: {}", directoryTocompact);

			// get and check the format version of the directory to compact.
			// If its not matching the Data Format of the current FDR instance Data format, 
			// then dont try to compact the older uncompacted directories as we dont know
			// its data format.
			uint32_t olderDirsDataFormat = CompactorGetItsDataFormat(directoryTocompact);
			if (olderDirsDataFormat != currentDataFormatVersion) {
            	fdrlog::info("uncompacted dir: {} with DataFormat: {} is not matching the current fdr DataFormat: {}. Skipping compaction!!",
					directoryTocompact, olderDirsDataFormat, currentDataFormatVersion);
				// finally update the compactor book keeper log entry saying it finished compacting the given window
				CompactorBookKeeperRemoveEntry(directoryTocompact);
				continue;
			}

            // call the compactor engine which does the rest of the compaction job
            CompactorEngine(directoryTocompact);
        }
    }
}

// This method looks through the compactor bookkeeper log file and finds if any N-2 directory is there uncompacted.
// If found, return that uncompacted directory name.
std::string FlightDataRecorder_c::CompactorGetDirectoryToCompact(int numberOfDirToLook)
{
	int counter = 0;
	std::string directoryTocompact = "";
	std::string logsformat = profile.GeneralConfig.LogsFormat;

    // Only used if encoding type is binary/json
    std::string logdir = profile.GeneralConfig.LogsBasePath + "/" + CommonFdrKeepersDirName + "/";
    std::string logfile = CompactorBookKeeperName;

	std::string compactorBookKeepLogFilepath;
	compactorBookKeepLogFilepath = logdir + logfile;

	// check if the file exists
	if (!(std::filesystem::exists(compactorBookKeepLogFilepath))) {
		fdrlog::warn("Compactor bookkeeper log file Not Exist!!: {}", compactorBookKeepLogFilepath);
		return "";
	}

	// have to slide through the bookKeeper
	fdrpb::fdr_compactor_bookkeep readrec;
	FDRStore CompactorBookKeeperReader(compactorBookKeepLogFilepath, profile.GeneralConfig.LogsFormat, STORE_READER);
	while (CompactorBookKeeperReader.readnext(&readrec)) {
		if (counter++ == 0) {
			directoryTocompact = readrec.compactdirectory();
		}
		// std::cout << "CompactorGetDirectoryToCompact: counter: " << counter
		// 		  << "; CompactDirectory: " << readrec.compactdirectory()
		// 		  << std::endl;

		if (counter >= numberOfDirToLook) {
			fdrlog::debug("CompactorGetDirectoryToCompact: counter reached. directoryTocompact: {}", directoryTocompact);
			break;
		}
	}

	return (counter >= numberOfDirToLook)?directoryTocompact: "";
}

// This method is the Compactor engine which executes the list of actions after every
// compaction window expiry[CompactionWindowSecs]
void FlightDataRecorder_c::CompactorEngine(std::string directoryTocompact)
{
    // in one stretch create the stat file for all the sensor types on all the devices[on its instances]
    uint64_t leastWindowTimestamp=0, farWindowTimestamp=0;

    // 1. get the from and to timestamp range
    CompactorGetLeastAndFarTimestamp(directoryTocompact, leastWindowTimestamp, farWindowTimestamp);
    // std::cout << "------------leastWindowTimestamp: " << leastWindowTimestamp
    // 		  << "; farWindowTimestamp: " << farWindowTimestamp << "------------" << std::endl;

    // 2. use leastWindowTimestamp, farWindowTimestamp and look through the bookOfErrors.dat file for
    //    any errors or events reported between these timestamps.
    std::vector<fdrpb::fdr_book_of_errors> errorList;
    if (CompactorCheckBookOfErrors(directoryTocompact, leastWindowTimestamp, farWindowTimestamp, errorList)) {
        fdrlog::warn ("Error found on directoryTocompact: {}",  directoryTocompact);
        // 2.1. in one stretch collect the high fidelity data if needed, and collect for all the
        //      sensor types on all the devices[and its instances]
        CompactorCreateHighFidelityFiles(directoryTocompact, errorList);
    } else {
		fdrlog::debug ("No Error found on directoryTocompact: {}", directoryTocompact);
    }

    // 3. after collecting and updating the high fidelity data, delete the .dat file
    CompactorRemoveSamplesLogfiles(directoryTocompact);

    // 4. finally update the compactor book keeper log entry saying it finished compacting the given window
    CompactorBookKeeperRemoveEntry(directoryTocompact);
}

