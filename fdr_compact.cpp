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
#include <filesystem>

// This method will slide through BookOfError.log file and find any error occured between the given 2 timestamps.
// If yes, then get the list of those errors between those 2 timestamps.
bool FlightDataRecorder_c::CompactorCheckBookOfErrors(const std::string directoryTocompact,
											 uint64_t leastWindowTimestamp,
											 uint64_t farWindowTimestamp,
											 std::vector<fdrpb::fdr_book_of_errors> &errorList)
{
	std::string bookOfErrorsFileName = profile.GeneralConfig.LogsBasePath + "/" + 
									   CommonFdrKeepersDirName + 
									   "/BookOfErrors.log";

	// check if the file exists
	if (!(std::filesystem::exists(bookOfErrorsFileName))) {
		log->warn("Book of errors file Not Exist!!: {}", bookOfErrorsFileName);
		//std::cout << "Book of errors file Not Exist!!: " << bookOfErrorsFileName << std::endl;
		return false;
	}

	auto splittedList = split(directoryTocompact, '_');
	if (splittedList.size() != 4) {
		// should never happen at this point..but checking for code completeness.
		log->warn("not a valid directoryTocompact: {}"
					   "; splittedList.size(): {}",
					   directoryTocompact,
					   splittedList.size());
		return false;
	}

	std::string givenBootId = splittedList[1];
	log->debug("CompactorCheckBookOfErrors: givenBootId: {}", givenBootId);

	// loop through the BookOfError.log file and check if any error occured during this given time period
	fdrpb::fdr_book_of_errors errorRecord;
	FDRStore BookOfErrorReader(bookOfErrorsFileName, profile.GeneralConfig.LogsFormat, STORE_READER);
	while (BookOfErrorReader.readnext(&errorRecord)) {
		// std::cout << "errorRecord: erroroccurtimeStamp: " << errorRecord.erroroccurtimestamp()
		// 		  << "; BootId: " << errorRecord.bootid()
		// 		  << "; SectionId: " << errorRecord.compclass()
		// 		  << "; ParamClass: " << errorRecord.paramclass()
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

	log->debug("timeSinceLastCompactionWindowDirCreation: {}"
	           "; LastCompactWindowDirCreationSecsAt: {}"
			   "; CompactionWindowSecs: {}",
			   timeSinceLastCompactionWindowDirCreation,
			   LastCompactWindowDirCreationSecsAt,
			   profile.GeneralConfig.CompactionWindowSecs);

	// Skip if its not time to compact yet
	if (viaTimerSkipChecks == true ||
		timeSinceLastCompactionWindowDirCreation >= profile.GeneralConfig.CompactionWindowSecs) {
		log->debug("======================creating new directory for sensors======================");
		// check for any compaction needs to be done and get the name of the directory to compact
		auto directoryTocompact = CompactorGetDirectoryToCompact(RUN_TIME_DIR_COUNT);
		log->debug("------------1. directoryTocompact: {}", directoryTocompact);
		if (directoryTocompact.empty()) {
			log->debug("------------Nothing to compact..Empty directoryTocompact!!------------");
		} else {
			log->debug("------------2. directoryTocompact: {}", directoryTocompact);

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

	log->debug("timeSinceLastCompactionSubWindowDirCreation: {}"
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
		log->debug("======================sub window timer expired======================");

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
		log->warn("not a valid directoryTocompact: {}"
					   "; splittedList.size(): {}",
					   directoryTocompact,
					   splittedList.size());
	} else {
		log->debug("CompactorGetLeastAndFarTimestamp: directory timestamp: {}", splittedList[3]);
		leastWindowTimestamp = std::strtoull(splittedList[3].c_str(), &endPtr, 10);
		if (*endPtr == '\0') {
			// std::cout << "Converted value: " << leastWindowTimestamp << std::endl;
			farWindowTimestamp = leastWindowTimestamp + profile.GeneralConfig.CompactionWindowSecs;
		} else {
			log->warn("Error converting string: {}", splittedList[3]);
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
	// 1. this loop will first collect all the required high fidelity data into a vector
	for (auto &section : profile.Sections) {
		for (auto &component : section.Components) {
			// collect the high fidelity logs for every instance of its components and
			// write it immdiately and clear the variable
			std::vector<fdrpb::fdr_sample> hifiRecords;

			for (auto &infogroup : component.InfoGroups) {
				if (infogroup.CompactionMethod != "Average") {
					continue; // Only numerical stats can be compacted not text etc. for now
				}

				if (profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_JSON || 
					profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_BINARY) {
					std::string logdir = profile.GeneralConfig.LogsBasePath + "/" + 
										 directoryTocompact + "/" +
										 section.ID + "/" +
										 component.ID + "/";

					// check is the directory exist
					// std::cout << "CompactorCreateHighFidelityFiles: 1. directory to compact: logdir: " << logdir << std::endl;
					if (!(std::filesystem::exists(logdir))) {
						log->warn("1. directory to compact Not Exist!!, logdir: {}", logdir);
						continue;
					}

					std::string logfile = logdir + infogroup.ID + ".log";
					fdrpb::fdr_sample readrec;

					// 1. read every record from the sensor*.log file
					FDRStore fdrLogSamplesReader(logfile, profile.GeneralConfig.LogsFormat, STORE_READER);
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
					std::string highFidelityFile = logdir + infogroup.ID + ".hifilog";
                	FDRStore fdrHifiWriter(highFidelityFile, profile.GeneralConfig.LogsFormat, STORE_WRITER);
					// loop through the hifiRecords and put them into the hifi log file
					for (auto &hifiRecord : hifiRecords) {
						fdrHifiWriter.append(hifiRecord);
					}
				}
			}
		}
	}
}

// This method will remove the *.log file after processing them for any errors and 
// collecting the hifi logs if any errors found.
void FlightDataRecorder_c::CompactorRemoveSamplesLogfiles(std::string directoryTocompact)
{
	// this loop will remove the .log file from all components from their respective directoryTocompact directories
	for (auto &section : profile.Sections) {
		for (auto &component : section.Components) {
			for (auto &infogroup : component.InfoGroups) {
				if (infogroup.CompactionMethod != "Average") {
					continue; // Only numerical stats can be compacted not text etc. for now
				}
				if (profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_JSON ||
					profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_BINARY) {
					std::string logdir = profile.GeneralConfig.LogsBasePath + "/" + 
										 directoryTocompact + "/" +
										 section.ID + "/" +
										 component.ID + "/";
					std::string logfiletodelete = logdir + infogroup.ID + ".log";

					// remove the file which contains all the samples
					if (remove(logfiletodelete.c_str()) != 0) {
						perror("CompactorRemoveSamplesLogfiles: Error deleting file");
						log->warn("CompactorRemoveSamplesLogfiles: Failed to delete: {}", logfiletodelete);
					} else {
						// std::cout << "CompactorRemoveSamplesLogfiles: Successfully deleted: " << logfiletodelete << std::endl;
					}
				}
			}
		}
	}
}

// This method will append to the BookKeepers/Compactor.log file with the new directory to be
// compacted in future[after the expiry of CompactionWindowSecs]
void FlightDataRecorder_c::CompactorBookKeeperAppendEntry(void) {
	fdrpb::fdr_compactor_bookkeep newEntry;
	newEntry.set_compactdirectory(GetDirectoryName());
	CompactorBookKeeperAppender->append(newEntry);
}

// This methos will remove a specified entry from BookKeepers/Compactor.log
void FlightDataRecorder_c::CompactorBookKeeperRemoveEntry(std::string directoryTocompact)
{
	std::string logsformat = profile.GeneralConfig.LogsFormat;
    std::string logdir = profile.GeneralConfig.LogsBasePath + "/" + CommonFdrKeepersDirName + "/";
    std::string logfile = CompactorBookKeeperName;

	std::string logfilepath;
    if (logsformat == ENCODING_CHOICE_BINARY || logsformat == ENCODING_CHOICE_JSON) {
        logfilepath = logdir + logfile;
    }

	// vectorize all the current entires from compactorBookKeeper.log file
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
        // then write the updated vectorized entires to the newCompactor.log file
        // [its first written to newCompactor.log and then copied to Compactor.log just to avoid the 
        // possible HMC reset while directly modifying Compactor.log and further inconsistency]
        std::string compactorNewBookKeeper = logdir + "new" + logfile;
        // 1. remove the new file if already exist
        if (remove(compactorNewBookKeeper.c_str()) == 0) {
            log->debug("Successfully deleted if any stray compactorNewBookKeeper: {}", 
                    	compactorNewBookKeeper);
        }

        // 2. write to the newCompactor.log
    	FDRStore fdrcompactorNewBookKeeperWriter(compactorNewBookKeeper, profile.GeneralConfig.LogsFormat, STORE_WRITER);
        for (auto &updatedBookofErrorRecord : vectorizedcompactorBookKeeper) {
            fdrcompactorNewBookKeeperWriter.append(updatedBookofErrorRecord);
        }

        // 3. copy newCompactor.log to Compactor.log
        std::string copyCommandStr = "cp " + compactorNewBookKeeper + " " + logfilepath;
        log->debug("copyCommandStr: {}", copyCommandStr);
        CommandResult_t cmdResult = exec(copyCommandStr.c_str());
        if (cmdResult.cmdExitstatus != FDR_SUCCESS) {
            log->warn("copy command Failed: {}", copyCommandStr);
            return;
        }
        log->debug("Successfully copied the new to current compactorBookKeeper file: {}", logfilepath);

        // 4. then remove the newCompactor.log
        if (remove(compactorNewBookKeeper.c_str()) == 0) {
            log->debug("Successfully deleted compactorNewBookKeeper: {}", compactorNewBookKeeper);
        }
    } else {
        log->debug("length of CompactorBookKeeperName: 0..so simply remove the file");
        // if no entries, then simply remove the Compactor.log
        if (remove(logfilepath.c_str()) == 0) {
            log->debug("Successfully deleted Compactor.log: {}", logfilepath);
        }
    }
}

// This method should be called only once when FDR starts.
// This method will scroll through the Compactor.log file for any entries which are not yet compacted
// in the previous FDR instance. It will get those entries one by one and compact each directories
// [like creating .stat files and hifilogs[if any error]] for those old entiries.
// Number of entries in Compactor.log will be 0 before this method returns.
void FlightDataRecorder_c::CompactorBookKeeperCleanEntries(void) {
    // loop for those many number of times as number of entries in the compactor bookkeeper log file
    for (;;) {
        // check for any compaction needs to be done and get the name of the directory to compact
        auto directoryTocompact = CompactorGetDirectoryToCompact(BOOT_TIME_DIR_COUNT);
        log->debug("------------BOOT_TIME: 1. directoryTocompact: {}", directoryTocompact);
        if (directoryTocompact.empty()) {
			log->debug("------------BOOT_TIME: Nothing to compact..Empty directoryTocompact!!------------");
            break;
        } else {
            log->debug("------------BOOT_TIME: 2. directoryTocompact: {}", directoryTocompact);

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
    if (logsformat == ENCODING_CHOICE_BINARY || logsformat == ENCODING_CHOICE_JSON) {
        compactorBookKeepLogFilepath = logdir + logfile;
    }

	// check if the file exists
	if (!(std::filesystem::exists(compactorBookKeepLogFilepath))) {
		log->warn("Compactor bookkeeper log file Not Exist!!: {}", compactorBookKeepLogFilepath);
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
			log->debug("CompactorGetDirectoryToCompact: counter reached. directoryTocompact: {}", directoryTocompact);
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

    // 2. use leastWindowTimestamp, farWindowTimestamp and look through the bookOfErrors.log file for
    //    any errors or events reported between these timestamps.
    std::vector<fdrpb::fdr_book_of_errors> errorList;
    if (CompactorCheckBookOfErrors(directoryTocompact, leastWindowTimestamp, farWindowTimestamp, errorList)) {
        log->warn ("Error found on directoryTocompact: {}",  directoryTocompact);
        // 2.1. in one stretch collect the high fidelity data if needed, and collect for all the
        //      sensor types on all the devices[and its instances]
        CompactorCreateHighFidelityFiles(directoryTocompact, errorList);
    } else {
		log->debug ("No Error found on directoryTocompact: {}", directoryTocompact);
    }

    // 3. after collecting and updating the high fidelity data, delete the .log file
    CompactorRemoveSamplesLogfiles(directoryTocompact);

    // 4. finally update the compactor book keeper log entry saying it finished compacting the given window
    CompactorBookKeeperRemoveEntry(directoryTocompact);
}

