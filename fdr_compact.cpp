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
		std::cout << "Book of errors file Not Exist!!: " << bookOfErrorsFileName << std::endl;
		return false;
	}

	auto splittedList = split(directoryTocompact, '_');
	if (splittedList.size() != 4) {
		// should never happen at this point..but checking for code completeness.
		std::cout << "not a valid directoryTocompact: " << directoryTocompact 
				  << "; splittedList.size(): " << splittedList.size() << std::endl;
		return false;
	}

	std::string givenBootId = splittedList[1];
	std::cout << "CompactorCheckBookOfErrors: givenBootId: " << givenBootId << std::endl;

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

// This method takes appropriate actions on every compaction window expiry
void FlightDataRecorder_c::CheckCompactionWindowExpiry()
{
	double timeSinceLastCompactionWindowDirCreation = difftime(std::time(nullptr), LastCompactWindowDirCreationSecsAt);

	std::cout << "timeSinceLastCompactionWindowDirCreation: " << timeSinceLastCompactionWindowDirCreation 
			  << "; LastCompactWindowDirCreationSecsAt: " << LastCompactWindowDirCreationSecsAt
			  << "; CompactionWindowSecs: " << profile.GeneralConfig.CompactionWindowSecs << std::endl;

	// Skip if its not time to compact yet
	if (timeSinceLastCompactionWindowDirCreation >= profile.GeneralConfig.CompactionWindowSecs) {
		std::cout << "======================creating new directory for sensors======================" << std::endl;
		// check for any compaction needs to be done and get the name of the directory to compact
		auto directoryTocompact = CompactorGetDirectoryToCompact(RUN_TIME_DIR_COUNT);
		std::cout << "------------1. directoryTocompact: " << directoryTocompact << "------------" << std::endl;
		if (directoryTocompact.empty()) {
			std::cout << "------------Nothing to compact..Empty directoryTocompact!!------------" << std::endl;
		} else {
			std::cout << "------------2. directoryTocompact: " << directoryTocompact << "------------" << std::endl;

            // call the compactor engine which does the rest of the compaction job
			CompactorEngine(directoryTocompact);
		}

		// update the global variable bootCounter and sensorDirTimestamp
		UpdateGlobVariables(false);

		// delete only the Sensor records
		// std::cout << "Before deleting-------------------------------------------------------------------------" << std::endl;
		// std::cout << "size of RecList:" << RecList.size() << std::endl;
		// for (auto recIt =  RecList.begin(); recIt !=  RecList.end(); ++recIt) { 
		// 	(*recIt)->Print();
		// } 
		DeleteSpecificRecords("Recreate");

		// create again the Sensor records with the new timestamp
		// std::cout << "after deleting-------------------------------------------------------------------------" << std::endl;
		// std::cout << "size of RecList:" << RecList.size() << std::endl;
		// for (auto recIt =  RecList.begin(); recIt !=  RecList.end(); ++recIt) { 
		// 	(*recIt)->Print();
		// } 
		CreateSpecificRecords("Recreate");

		// std::cout << "after creating-------------------------------------------------------------------------" << std::endl;
		// std::cout << "size of RecList:" << RecList.size() << std::endl;
		// for (auto recIt =  RecList.begin(); recIt !=  RecList.end(); ++recIt) { 
		// 	(*recIt)->Print();
		// } 

		// after creating the specific records again, update the Compactor BookKeeper
		CompactorBookKeeperAppendEntry();

		// reset to the current time
		LastCompactWindowDirCreationSecsAt = std::time(nullptr);
	}
}

// This method will append a new stat entry on each subwindow into its respective stat file
void FlightDataRecorder_c::CompactorAppendStatFile(FDRStore &fdrstatsWriter, std::map<unsigned int, fdrpb::fdr_stat> stats_so_far)
{
	for (auto &stat : stats_so_far)	{
		stat.second.set_avg(stat.second.avg() / stat.second.numsamples());

		// std::cout << "-------------------------------------------------------------------" << std::endl;
		// std::cout << "appending stats to file: " << statFileName << std::endl;

		fdrstatsWriter.append(stat.second);
	}
}

// This method will create the stat file for all the types on all the device [and its instances].
// Also this method while sliding through this compact window directory finds the least timestamp and the far timestamp
// [which will latter updated into their corresponding entry compactor book keeper file by the caller]
void FlightDataRecorder_c::CompactorCreateStatFiles(const std::string directoryTocompact,
									uint64_t &leastWindowTimestamp, uint64_t &farWindowTimestamp)
{
	// reset the variable references
	leastWindowTimestamp = 0;
	farWindowTimestamp = 0;

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

					// check is the directory exist
					// std::cout << "CompactorCreateStatFiles: directory to compact: logdir: " << logdir << std::endl;
					if (!(std::filesystem::exists(logdir))) {
						std::cout << "directory to compact Not Exist!!, logdir: " << logdir << std::endl;
						continue;
					}

					std::string logfile = logdir + infogroup.ID + ".log";
					std::string statsfile = logdir + infogroup.ID + ".stats";
					fdrpb::fdr_sample readrec;
					int statCounter = 0;
					uint64_t startSubWindowTime = 0, endSubWindowTime = 0, currentRecordTime = 0;
					std::map<unsigned int, fdrpb::fdr_stat> stats_so_far;

					// read every record from the *.log file
					FDRStore fdrLogSamplesReader(logfile, profile.GeneralConfig.LogsFormat, STORE_READER);
					FDRStore fdrstatsWriter(statsfile, profile.GeneralConfig.LogsFormat, STORE_WRITER);
					while (fdrLogSamplesReader.readnext(&readrec)) {
						// 1. reset the sliding window
						if (statCounter == 0) {
							startSubWindowTime = readrec.timestamp();
							endSubWindowTime = startSubWindowTime + profile.GeneralConfig.CompactionSubWindowSecs;
							currentRecordTime = readrec.timestamp();
						} else {
							currentRecordTime = readrec.timestamp();
						}
						statCounter++;

						// 2. [for bookkeeping] find the leastWindowTimestamp and farWindowTimestamp in this window
						if (farWindowTimestamp != 0) {
							farWindowTimestamp = std::max(farWindowTimestamp, currentRecordTime);
						} else {
							// first time updating the variables
							leastWindowTimestamp = farWindowTimestamp = currentRecordTime;
						}
						// std::cout << "leastWindowTimestamp: " << leastWindowTimestamp
						// 		  << "; farWindowTimestamp: " << farWindowTimestamp << std::endl;

						// std::cout << "endSubWindowTime: " << endSubWindowTime 
						// 		  << "; profile.GeneralConfig.CompactionSubWindowSecs: " << profile.GeneralConfig.CompactionSubWindowSecs 
						// 		  << "; currentRecordTime: " << currentRecordTime << std::endl;

						// 3. check if slided beyond the compact subwindow
						if (endSubWindowTime < currentRecordTime) {
							// 3.1: append to the statfile
							CompactorAppendStatFile(fdrstatsWriter, stats_so_far);
							
							// 3.2: then clear all the map entires to start collecting freshly the next subwindow sliding
							stats_so_far.clear();

							// 3.3: then reset the statCounter
							statCounter = 0;
						}

						// 4. collect all the stats in this sliding subwindow
						stats_so_far[readrec.paramid()].set_paramid(readrec.paramid());
						stats_so_far[readrec.paramid()].set_numsamples(stats_so_far[readrec.paramid()].numsamples() + 1);
						stats_so_far[readrec.paramid()].set_avg(stats_so_far[readrec.paramid()].avg() + readrec.paramvalueint64()); // TODO: using avg field as sum. avoid overflow.
						if (stats_so_far[readrec.paramid()].min() != 0) {
							stats_so_far[readrec.paramid()].set_min(std::min(stats_so_far[readrec.paramid()].min(), readrec.paramvalueint64()));
						} else {
							stats_so_far[readrec.paramid()].set_min(readrec.paramvalueint64());
						}
						stats_so_far[readrec.paramid()].set_max(std::max(stats_so_far[readrec.paramid()].max(), readrec.paramvalueint64()));
						stats_so_far[readrec.paramid()].set_fromtime(stats_so_far[readrec.paramid()].fromtime() == 0 ? currentRecordTime : stats_so_far[readrec.paramid()].fromtime());
						stats_so_far[readrec.paramid()].set_totime(currentRecordTime);
					}

					// 5. flush the remaining leftover entires to the statfile
					// std::cout << "--------flush the remaining leftover entires to the statfile: stats_so_far.size(): " << stats_so_far.size() << "-------------" << std::endl;
					if (stats_so_far.size() != 0) {
						CompactorAppendStatFile(fdrstatsWriter, stats_so_far);
					}
				} else if (profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_DB) {
					// TODO: not yet coded this section
				}
			}
		}
	}
}

void FlightDataRecorder_c::CompactorCollectHiFidelityRecords(
				std::string componentId,
				std::string infogroupID,
				fdrpb::fdr_sample readRecord,
				std::map<std::string, std::map<std::string, std::vector<fdrpb::fdr_sample>>> &hifiRecords)
{
	if (hifiRecords[componentId].size() == 0 || hifiRecords[componentId][infogroupID].size() == 0) {
		// then simply push the readRecord back at the vector
		hifiRecords[componentId][infogroupID].push_back(readRecord);
	} else {
		for (auto &componentVector : hifiRecords[componentId][infogroupID]) {
			// check if the incoming record already exist in the vector
			if (componentVector.timestamp() == readRecord.timestamp() &&
				componentVector.paramid() == readRecord.paramid()) {
				if (componentVector.paramvalueint64() == readRecord.paramvalueint64()) {
					// std::cout << "-----------int64: value matches in one of the record";
					return;
				}
			}
		}
		// if it reaches here, then it means this record is unique and not yet collected..so collect it
		hifiRecords[componentId][infogroupID].push_back(readRecord);
		// std::cout << "5. final else case: length: " << hifiRecords[componentId][infogroupID].size() << std::endl;
	}
}

// This method will scans through the samples log files and check if the record falls in any
// of the errorlist range. If yes, then collect the High Fidelity logs and writes the
// collected logs into a separate *.hifilog file.
void FlightDataRecorder_c::CompactorCreateHighFidelityFiles(const std::string directoryTocompact,
									std::vector<fdrpb::fdr_book_of_errors> errorList)
{
	std::map<std::string, std::map<std::string, std::vector<fdrpb::fdr_sample>>> hifiRecords;

	// 1. this loop will first collect all the required high fidelity data into a vector
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

					// check is the directory exist
					// std::cout << "CompactorCreateHighFidelityFiles: 1. directory to compact: logdir: " << logdir << std::endl;
					if (!(std::filesystem::exists(logdir))) {
						std::cout << "1. directory to compact Not Exist!!, logdir: " << logdir << std::endl;
						continue;
					}

					std::string logfile = logdir + infogroup.ID + ".log";
					fdrpb::fdr_sample readrec;

					// read every record from the sensor*.log file
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
								CompactorCollectHiFidelityRecords(component.ID, infogroup.ID, readrec, hifiRecords);
							}
						}
					}
				} else if (profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_DB) {
					// TODO: not yet coded this section
				}
			}
		}
	}

	// 2. this loop will write the collected data into their respective directories
	for (auto &section : profile.Sections) {
		for (auto &component : section.Components) {
			for (auto &infogroup : component.InfoGroups) {
				if (infogroup.CompactionMethod != "Average") {
					continue; // Only numerical stats can be compacted not text etc. for now
				}

				// check if any record got collected for this component
				if (hifiRecords[component.ID][infogroup.ID].size() == 0) {
					// nothing..then continue to next item
					std::cout << "No hifi collected for this component: " << component.ID 
							  << "; infogroup: " << infogroup.ID 
							  << std::endl;
					continue;
				}
				if (profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_JSON ||
					profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_BINARY) {
					std::string logdir = profile.GeneralConfig.LogsBasePath + "/" + 
										 directoryTocompact + "/" +
										 section.ID + "/" +
										 component.ID + "/";

					// check is the directory exist
					// std::cout << "CompactorCreateHighFidelityFiles: 2.directory to compact: logdir: " << logdir << std::endl;
					if (!(std::filesystem::exists(logdir))) {
						std::cout << "2. directory to compact Not Exist!!, logdir: " << logdir << std::endl;
						continue;
					}

					std::string highFidelityFile = logdir + infogroup.ID + ".hifilog";
					fdrpb::fdr_sample readrec;
                	FDRStore fdrHifiWriter(highFidelityFile, profile.GeneralConfig.LogsFormat, STORE_WRITER);
					// loop through the hifiRecords and put them into the hifi log file
					for (auto &hifiRecord : hifiRecords[component.ID][infogroup.ID]) {
						fdrHifiWriter.append(hifiRecord);
					}
				} else if (profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_DB) {
					// TODO: not yet coded this section
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
						std::cout << "CompactorRemoveSamplesLogfiles: Failed to delete: " << logfiletodelete << std::endl;
					} else {
						// std::cout << "CompactorRemoveSamplesLogfiles: Successfully deleted: " << logfiletodelete << std::endl;
					}
				} else if (profile.GeneralConfig.LogsFormat == ENCODING_CHOICE_DB) {
					// TODO: not yet coded this section
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
    if (logsformat == ENCODING_CHOICE_DB) {
        logfilepath = profile.GeneralConfig.LogsBasePath + "/" + profile.GeneralConfig.DatabaseName;
    } else if (logsformat == ENCODING_CHOICE_BINARY || logsformat == ENCODING_CHOICE_JSON) {
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
            std::cout << "Successfully deleted if any stray compactorNewBookKeeper: " 
                    << compactorNewBookKeeper << std::endl;
        }

        // 2. write to the newCompactor.log
    	FDRStore fdrcompactorNewBookKeeperWriter(compactorNewBookKeeper, profile.GeneralConfig.LogsFormat, STORE_WRITER);
        for (auto &updatedBookofErrorRecord : vectorizedcompactorBookKeeper) {
            fdrcompactorNewBookKeeperWriter.append(updatedBookofErrorRecord);
        }

        // 3. copy newCompactor.log to Compactor.log
        std::string copyCommandStr = "cp " + compactorNewBookKeeper + " " + logfilepath;
        std::cout << "copyCommandStr: " << copyCommandStr << std::endl;
        CommandResult_t cmdResult = exec(copyCommandStr.c_str());
        if (cmdResult.cmdExitstatus != FDR_SUCCESS) {
            std::cout << "copy command Failed: " << copyCommandStr << std::endl;
            return;
        }
        std::cout << "Successfully copied the new to current compactorBookKeeper file: " << logfilepath << std::endl;

        // 4. then remove the newCompactor.log
        if (remove(compactorNewBookKeeper.c_str()) == 0) {
            std::cout << "Successfully deleted compactorNewBookKeeper: " << compactorNewBookKeeper << std::endl;
        }
    } else {
        std::cout << "length of CompactorBookKeeperName: 0..so simply remove the file" << std::endl;
        // if no entries, then simply remove the Compactor.log
        if (remove(logfilepath.c_str()) == 0) {
            std::cout << "Successfully deleted Compactor.log: " << logfilepath << std::endl;
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
        std::cout << "------------BOOT_TIME: 1. directoryTocompact: " << directoryTocompact << "------------" << std::endl;
        if (directoryTocompact.empty()) {
            std::cout << "------------BOOT_TIME: Nothing to compact..Empty directoryTocompact!!------------" << std::endl;
            break;
        } else {
            std::cout << "------------BOOT_TIME: 2. directoryTocompact: " << directoryTocompact << "------------" << std::endl;

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
    if (logsformat == ENCODING_CHOICE_DB) {
        compactorBookKeepLogFilepath = profile.GeneralConfig.LogsBasePath + "/" + profile.GeneralConfig.DatabaseName;
    } else if (logsformat == ENCODING_CHOICE_BINARY || logsformat == ENCODING_CHOICE_JSON) {
        compactorBookKeepLogFilepath = logdir + logfile;
    }

	// check if the file exists
	if (!(std::filesystem::exists(compactorBookKeepLogFilepath))) {
		std::cout << "Compactor bookkeeper log file Not Exist!!: " << compactorBookKeepLogFilepath << std::endl;
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
			std::cout << "CompactorGetDirectoryToCompact: counter reached 2. directoryTocompact: " << directoryTocompact << std::endl;
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

    // 1. create the stat files
    CompactorCreateStatFiles(directoryTocompact, leastWindowTimestamp, farWindowTimestamp);
    // std::cout << "------------leastWindowTimestamp: " << leastWindowTimestamp
    // 		  << "; farWindowTimestamp: " << farWindowTimestamp << "------------" << std::endl;

    // 2. use leastWindowTimestamp, farWindowTimestamp and look through the bookOfErrors.log file for
    //    any errors or events reported between these timestamps.
    std::vector<fdrpb::fdr_book_of_errors> errorList;
    if (CompactorCheckBookOfErrors(directoryTocompact, leastWindowTimestamp, farWindowTimestamp, errorList)) {
        std::cout << "Error found on directoryTocompact: " << directoryTocompact << std::endl;
        // 2.1. in one stretch collect the high fidelity data if needed for all the
        //      sensor types on all the devices[and its instances]
        CompactorCreateHighFidelityFiles(directoryTocompact, errorList);
    } else {
        std::cout << "No Error found on directoryTocompact: " << directoryTocompact << std::endl;
    }

    // 3. after collecting and updating the high fidelity data, delete the .log file
    CompactorRemoveSamplesLogfiles(directoryTocompact);

    // 4. finally update the compactor book keeper log entry saying it finished compacting the given window
    CompactorBookKeeperRemoveEntry(directoryTocompact);
}

