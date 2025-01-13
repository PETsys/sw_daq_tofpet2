#include "DataFileWriter.hpp"
#include <TFile.h>
#include <TNtuple.h>

using namespace PETSYS;

DataFileWriter::DataFileWriter(char *fName, double frequency, EVENT_TYPE eventType, FILE_TYPE fileType, double fileEpoch, int hitLimitToWrite, int eventFractionToWrite, float splitTime){
    this->fName = std::string(fName);
    this->fileType = (strcmp(fName, "/dev/null") != 0) ? fileType : FILE_NULL;
    this->fileEpoch = fileEpoch;
    this->eventType = eventType;
    this->eventFractionToWrite = eventFractionToWrite;
    this->eventCounter = 0;

    this->fileSplitTime = splitTime * frequency; // Convert from seconds to clock cycles
    this->currentFilePartIndex = 0;

    this->hitLimitToWrite = hitLimitToWrite;

    this->Tps = 1E12/frequency;
    this->Tns = Tps / 1000.;

    openFile();
};

void DataFileWriter::openFile() {
    stepBegin = 0;
    
    if (fileType == FILE_ROOT){
        hFile = new TFile(fName.c_str(), "RECREATE");
        int bs = 512*1024;

        hData = new TTree("data", "Event List", 2);
        hData->Branch("step1", &brStep1, bs);
        hData->Branch("step2", &brStep2, bs);

        if(eventType == RAW){
            hData->Branch("frameID", &brFrameID, bs);
			hData->Branch("channelID", &brChannelID, bs);
			hData->Branch("tacID", &brTacID, bs);
			hData->Branch("tcoarse", &brTCoarse, bs);
			hData->Branch("ecoarse", &brECoarse, bs);
			hData->Branch("tfine", &brTFine, bs);
			hData->Branch("efine", &brEFine, bs);
        }
        if(eventType == SINGLE || eventType == GROUP){  
            
            hData->Branch("time", &brTime, bs);
            hData->Branch("channelID", &brChannelID, bs);
            hData->Branch("tot", &brToT, bs);
            hData->Branch("energy", &brEnergy, bs);
            hData->Branch("tacID", &brTacID, bs);
            hData->Branch("xi", &brXi, bs);
            hData->Branch("yi", &brYi, bs);
            hData->Branch("x", &brX, bs);
            hData->Branch("y", &brY, bs);
            hData->Branch("z", &brZ, bs);
            hData->Branch("tqT", &brTQT, bs);
            hData->Branch("tqE", &brTQE, bs);
        }
        if(eventType == GROUP){
            hData->Branch("timeDelta", &brTimeDelta, bs);
            hData->Branch("mh_n", &brN, bs);
            hData->Branch("mh_j", &brJ, bs);
        }
        if(eventType == COINCIDENCE){
            hData->Branch("mh_n1", &br1N, bs);
            hData->Branch("mh_j1", &br1J, bs);
            hData->Branch("tot1", &br1ToT, bs);
            hData->Branch("time1", &br1Time, bs);
            hData->Branch("channelID1", &br1ChannelID, bs);
            hData->Branch("energy1", &br1Energy, bs);
            hData->Branch("tacID1", &br1TacID, bs);
            hData->Branch("xi1", &br1Xi, bs);
            hData->Branch("yi1", &br1Yi, bs);
            hData->Branch("x1", &br1X, bs);
            hData->Branch("y1", &br1Y, bs);
            hData->Branch("z1", &br1Z, bs);
            hData->Branch("mh_n2", &br2N, bs);
            hData->Branch("mh_j2", &br2J, bs);
            hData->Branch("time2", &br2Time, bs);
            hData->Branch("channelID2", &br2ChannelID, bs);
            hData->Branch("tot2", &br2ToT, bs);
            hData->Branch("energy2", &br2Energy, bs);
            hData->Branch("tacID2", &br2TacID, bs);
            hData->Branch("xi2", &br2Xi, bs);
            hData->Branch("yi2", &br2Yi, bs);
            hData->Branch("x2", &br2X, bs);
            hData->Branch("y2", &br2Y, bs);
            hData->Branch("z2", &br2Z, bs);
        }

        hIndex = new TTree("index", "Step Index", 2);
        hIndex->Branch("step1", &brStep1, bs);
        hIndex->Branch("step2", &brStep2, bs);
        hIndex->Branch("stepBegin", &brStepBegin, bs);
        hIndex->Branch("stepEnd", &brStepEnd, bs);
    }
    else if(fileType == FILE_BINARY || fileType == FILE_BINARY_COMPACT) {
        char fName2[1024];
        sprintf(fName2, "%s.ldat", fName.c_str());
        dataFile = fopen(fName2, "w");
        sprintf(fName2, "%s.lidx", fName.c_str());
        indexFile = fopen(fName2, "w");
        assert(dataFile != NULL);
        assert(indexFile != NULL);
    }
    else if(fileType == FILE_TEXT || fileType == FILE_TEXT_COMPACT) {
        dataFile = fopen(fName.c_str(), "w");
        assert(dataFile != NULL);
        indexFile = NULL;
    }
};
	
DataFileWriter::~DataFileWriter() {
    closeFile();
    if(fileSplitTime > 0) {
        renameFile();
    }
};

void DataFileWriter::setStepValues(float step1, float step2){
	this->step1 = step1;
    this->step2 = step2;
}

void DataFileWriter::closeFile() {
    if (fileType == FILE_ROOT){
        hFile->Write();
        hFile->Close();
    }
    else if(fileType == FILE_BINARY || fileType == FILE_BINARY_COMPACT) {
        fclose(dataFile);
        fclose(indexFile);
    }
    else if(fileType == FILE_TEXT || fileType == FILE_TEXT_COMPACT) {
        fclose(dataFile);
    }
}

void DataFileWriter::closeStep() {
    if (fileType == FILE_ROOT){
        brStepBegin = stepBegin;
        brStepEnd = hData->GetEntries();
        brStep1 = this->step1;
        brStep2 = this->step2;
        hIndex->Fill();
        stepBegin = hData->GetEntries();
        hFile->Write();
    }
    else if(fileType == FILE_BINARY || fileType == FILE_BINARY_COMPACT) {
        fprintf(indexFile, "%ld\t%ld\t%e\t%e\n", stepBegin, ftell(dataFile), this->step1, this->step2);
        stepBegin = ftell(dataFile);
    }
    else {
        // Do nothing
    }
}

void DataFileWriter::checkFilePartForSplit(long long filePartIndex) {
    if((fileSplitTime > 0) && (filePartIndex > currentFilePartIndex)) {
        closeStep();
        closeFile();
        renameFile();
        openFile();
        currentFilePartIndex = filePartIndex;
    }
}   

void DataFileWriter::renameFile() {
    char *fName1 = new char[1024];
    char *fName2 = new char[1024];
    if(fileType == FILE_BINARY || fileType == FILE_BINARY_COMPACT) {
        // Binary output consists of two files and fName is their common prefix

        sprintf(fName1, "%s.ldat", fName.c_str());
        sprintf(fName2, "%s_%08lld.ldat", fName.c_str(), currentFilePartIndex);
        int r = rename(fName1, fName2);
        assert(r == 0);

        sprintf(fName1, "%s.lidx", fName.c_str());
        sprintf(fName2, "%s_%08lld.lidx", fName.c_str(), currentFilePartIndex);
        r = rename(fName1, fName2);
        assert(r == 0);

    }
    else {
        // ROOT or text output consists of a single file and fName is the complete fileName
        strcpy(fName1, fName.c_str());
        char *p = rindex(fName1, '.');

        if(p == NULL) {
            // If fName lacks a "." append the file part number at the end of the file name
            sprintf(fName2, "%s_%08lld", fName1, currentFilePartIndex);
            int r = rename(fName1, fName2);
            assert(r == 0);
        }
        else {
            // Insert the file part number before the extension
            char tmp = *p;
            *p = '\0';
            sprintf(fName2, "%s_%08lld.%s", fName1, currentFilePartIndex, p+1);
            *p = tmp;
            int r = rename(fName1, fName2);
            assert(r == 0);
        }

    }
    delete [] fName2;
    delete [] fName1;
};


void DataFileWriter::writeRawEvents(EventBuffer<RawHit> *buffer, double t0) {
    
    long long filePartIndex = (int)floor(buffer->getTMin() / fileSplitTime);
    checkFilePartForSplit(filePartIndex);
    
    int N = buffer->getSize();

    long long bufferMinFrameID = buffer->getTMin() / 1024;

    for (int i = 0; i < N; i++) {
        long long tmpCounter = eventCounter;
        eventCounter += 1;
        if((tmpCounter % 1024) >= eventFractionToWrite) continue;

        RawHit &hit = buffer->get(i);
        if (fileType == FILE_ROOT){
            brStep1 = step1;
            brStep2 = step2;
            
            brFrameID = hit.frameID + bufferMinFrameID;
            brChannelID = hit.channelID;
            brTacID = hit.tacID;
            brTCoarse = hit.tcoarse;
            brECoarse = hit.ecoarse;
            brTFine = hit.tfine;
            brEFine = hit.efine;
            hData->Fill();
        }
        else {
            fprintf(dataFile, "%lu\t%u\t%hu\t%hu\t%hu\t%hu\t%hu\n",
                hit.frameID,
                hit.channelID, hit.tacID,
                hit.tcoarse, hit.ecoarse,
                hit.tfine, hit.efine
            );
        }
    }	
}


void DataFileWriter::writeSingleEvents(EventBuffer<Hit> *buffer, double t0) {
    
    long long filePartIndex = (int)floor(buffer->getTMin() / fileSplitTime);
    checkFilePartForSplit(filePartIndex);
    
    long long tMin = (buffer->getTMin() + t0 - fileEpoch) * (long long)Tps;

    int N = buffer->getSize();
    for (int i = 0; i < N; i++) {
        long long tmpCounter = eventCounter;
        eventCounter += 1;
        if((tmpCounter % 1024) >= eventFractionToWrite) continue;

        Hit &hit = buffer->get(i);
        if(!hit.valid) continue;

        float Eunit = hit.raw->qdcMode ? 1.0 : Tns;
        
        if (fileType == FILE_ROOT){
            brStep1 = step1;
            brStep2 = step2;
            
            brTime = ((long long)(hit.time * Tps)) + tMin;
            brChannelID = hit.raw->channelID;
            brToT = (hit.timeEnd - hit.time) * Tps;
            brEnergy = hit.energy * Eunit;
            brTacID = hit.raw->tacID;
            brTQT = hit.raw->time - hit.time;
            brTQE = (hit.raw->timeEnd - hit.timeEnd);
            brX = hit.x;
            brY = hit.y;
            brZ = hit.z;
            brXi = hit.xi;
            brYi = hit.yi;
            
            hData->Fill();
        }
        else if(fileType == FILE_BINARY) {
            Event eo = {
                ((long long)(hit.time * Tps)) + tMin,
                hit.energy * Eunit,
                (int)hit.raw->channelID
            };
            fwrite(&eo, sizeof(eo), 1, dataFile);
        }
        else if (fileType == FILE_TEXT) {
            fprintf(dataFile, "%lld\t%f\t%d\n",
                ((long long)(hit.time * Tps)) + tMin,
                hit.energy * Eunit,
                (int)hit.raw->channelID
                );
        }
    }	
}

void DataFileWriter::writeGroupEvents(EventBuffer<GammaPhoton> *buffer, double t0) {
    long long filePartIndex = (int)floor(buffer->getTMin() / fileSplitTime);
    checkFilePartForSplit(filePartIndex);

    long long tMin = (buffer->getTMin() + t0) * (long long)Tps;
    
    int N = buffer->getSize();
    for (int i = 0; i < N; i++) {
        long long tmpCounter = eventCounter;
        eventCounter += 1;
        if((tmpCounter % 1024) >= eventFractionToWrite) continue;

        GammaPhoton &p = buffer->get(i);
        
        if(!p.valid) continue;

        Hit &h0 = *p.hits[0];
        int limit = (hitLimitToWrite < p.nHits) ? hitLimitToWrite : p.nHits;

        if(fileType == FILE_TEXT_COMPACT) {
            fprintf(dataFile, "%d\n", limit);
        }
        else if(fileType == FILE_BINARY_COMPACT) {
            GroupHeader header = {(uint8_t)limit};
            fwrite(&header, sizeof(header), 1, dataFile);
        }

        for(int m = 0; m < limit; m++) {
            Hit &h = *p.hits[m];
            float Eunit = h.raw->qdcMode ? 1.0 : Tns;


            if (fileType == FILE_ROOT){
                brStep1 = step1;
                brStep2 = step2;

                brN  = p.nHits;
                brJ = m;
                brTime = ((long long)(h.time * Tps)) + tMin;
                brTimeDelta = (long long)((h.time - h0.time) * Tps);
                brChannelID = h.raw->channelID;
                brToT = (h.timeEnd - h.time) * Tps;
                brEnergy = h.energy * Eunit;
                brTacID = h.raw->tacID;
                brX = h.x;
                brY = h.y;
                brZ = h.z;
                brXi = h.xi;
                brYi = h.yi;
                
                hData->Fill();
            }
            else if(fileType == FILE_BINARY) {
                GroupEvent eo = { 
                    (uint8_t)p.nHits, (uint8_t)m,
                    ((long long)(h.time * Tps)) + tMin,
                    h.energy * Eunit,
                    (int)h.raw->channelID
                };
                fwrite(&eo, sizeof(eo), 1, dataFile);
            }
            else if(fileType == FILE_BINARY_COMPACT) {
                Event eo = {
                    ((long long)(h.time * Tps)) + tMin,
                    h.energy * Eunit,
                    (int)h.raw->channelID
                };
                fwrite(&eo, sizeof(eo), 1, dataFile);
            }
            else if (fileType == FILE_TEXT) {
                fprintf(dataFile, "%d\t%d\t%lld\t%f\t%d\n",
                    p.nHits, m,
                    ((long long)(h.time * Tps)) + tMin,
                    h.energy * Eunit,
                    h.raw->channelID
                );
            }
            else if (fileType == FILE_TEXT_COMPACT) {
                fprintf(dataFile, "%lld\t%f\t%d\n",
                    ((long long)(h.time * Tps)) + tMin,
                    h.energy * Eunit,
                    h.raw->channelID
                );
            }
        }
    }	   
}


void DataFileWriter::writeCoincidenceEvents(EventBuffer<Coincidence> *buffer, double t0) {
    long long filePartIndex = (int)floor(buffer->getTMin() / fileSplitTime);
    checkFilePartForSplit(filePartIndex);

    long long tMin = (buffer->getTMin() + t0) * (long long)Tps;
    
    int N = buffer->getSize();
    for (int i = 0; i < N; i++) {
        long long tmpCounter = eventCounter;
        eventCounter += 1;
        if((tmpCounter % 1024) >= eventFractionToWrite) continue;

        Coincidence &e = buffer->get(i);
        if(!e.valid) continue;
        if(e.nPhotons != 2) continue;
        
        GammaPhoton &p1 = *e.photons[0];
        GammaPhoton &p2 = *e.photons[1];
        
        int limit1 = (hitLimitToWrite < p1.nHits) ? hitLimitToWrite : p1.nHits;
        int limit2 = (hitLimitToWrite < p2.nHits) ? hitLimitToWrite : p2.nHits;

        int coincHitIndex = 0;
        
        if(fileType == FILE_TEXT_COMPACT) {	
            fprintf(dataFile, "%d\t%d\n", limit1, limit2);
            for(int i = 0; i < limit1 + limit2; i++) {
                Hit &h = i < limit1 ? *p1.hits[i] : *p2.hits[i-limit1];
                float Eunit = h.raw->qdcMode ? 1.0 : Tns;
                fprintf(dataFile, "%lld\t%f\t%d\n",
                ((long long)(h.time * Tps)) + tMin,
                h.energy * Eunit,
                h.raw->channelID);
            }
        }
        else if(fileType == FILE_BINARY_COMPACT) {
            CoincidenceGroupHeader header = {(uint8_t)limit1, (uint8_t)limit2};
            fwrite(&header, sizeof(header), 1, dataFile);
            for(int i = 0; i < limit1 + limit2; i++) {
                Hit &h = i < limit1 ? *p1.hits[i] : *p2.hits[i-limit1];
                float Eunit = h.raw->qdcMode ? 1.0 : Tns;
                Event eo = { 
                    ((long long)(h.time * Tps)) + tMin,
                    h.energy * Eunit,
                    (int)h.raw->channelID};
                fwrite(&eo, sizeof(eo), 1, dataFile);
            }
        }
        else{
            for(int m = 0; m < limit1; m++) for(int n = 0; n < limit2; n++) {
                if(m != 0 && n != 0) continue;
                
                Hit &h1 = *p1.hits[m];
                Hit &h2 = *p2.hits[n];
                
                float Eunit1 = h1.raw->qdcMode ? 1.0 : Tns;
                float Eunit2 = h2.raw->qdcMode ? 1.0 : Tns;

                if (fileType == FILE_ROOT){
                    brStep1 = this->step1;
                    brStep2 = this->step2;

                    br1N  = p1.nHits;
                    br1J = m;
                    br1Time = ((long long)(h1.time * Tps)) + tMin;
                    br1ChannelID = h1.raw->channelID;
                    br1ToT = (h1.timeEnd - h1.time) * Tps;
                    br1Energy = h1.energy * Eunit1;
                    br1TacID = h1.raw->tacID;
                    br1X = h1.x;
                    br1Y = h1.y;
                    br1Z = h1.z;
                    br1Xi = h1.xi;
                    br1Yi = h1.yi;

                    br2N  = p2.nHits;
                    br2J = n;
                    br2Time = ((long long)(h2.time * Tps)) + tMin;
                    br2ChannelID = h2.raw->channelID;
                    br2ToT = (h2.timeEnd - h2.time) * Tps;
                    br2Energy = h2.energy * Eunit2;
                    br2TacID = h2.raw->tacID;
                    br2X = h2.x;
                    br2Y = h2.y;
                    br2Z = h2.z;
                    br2Xi = h2.xi;
                    br2Yi = h2.yi;

                    hData->Fill();
                }
                else if(fileType == FILE_BINARY) {
                    CoincidenceEvent eo = { 
                        (uint8_t)p1.nHits, (uint8_t)m,
                        ((long long)(h1.time * Tps)) + tMin,
                        h1.energy * Eunit1,
                        (int)h1.raw->channelID,
                        (uint8_t)p2.nHits, (uint8_t)n,
                        ((long long)(h2.time * Tps)) + tMin,
                        h2.energy * Eunit2,
                        (int)h2.raw->channelID		
                    };
                    fwrite(&eo, sizeof(eo), 1, dataFile);
                }
                else if(fileType == FILE_TEXT) {
                    fprintf(dataFile, "%d\t%d\t%lld\t%f\t%d\t%d\t%d\t%lld\t%f\t%d\n",
                        p1.nHits, m,
                        ((long long)(h1.time * Tps)) + tMin,
                        h1.energy * Eunit1,
                        h1.raw->channelID,

                        p2.nHits, n,
                        ((long long)(h2.time * Tps)) + tMin,
                        h2.energy * Eunit2,
                        h2.raw->channelID
                    );
                }
            }
        }
    }	
};
