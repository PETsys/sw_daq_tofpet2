"""
Shared memory reader for online monitoring data.

Struct layout:
    uint32_t nChannels
    uint32_t nEnergyBins
    double   energyLow
    double   energyHigh
    uint64_t counts[nChannels * nEnergyBins]
"""
 
import os
import mmap
import struct
import numpy as np
import time
 
 
HEADER_FORMAT = "II dd"  # nChannels(u32), nEnergyBins(u32), energyLow(f64), energyHigh(f64)
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)  # 4 + 4 + 8 + 8 = 24 bytes
 
 
class MonitoringShmReader:
    def __init__(self, shm_name="/online_data"):
        self.fd  = None
        self.buf = None

        shm_path = f"/dev/shm{shm_name}"

        try:
            self.fd       = os.open(shm_path, os.O_RDONLY)
            file_size     = os.fstat(self.fd).st_size
            self.buf      = mmap.mmap(self.fd, file_size, access=mmap.ACCESS_READ)
        except OSError as e:
            self.close()
            raise OSError(f"Failed to open shared memory '{shm_path}': {e}") from None

        try:
            self.nChannels, self.nEnergyBins, self.energyLow, self.energyHigh = struct.unpack_from(HEADER_FORMAT, self.buf, 0)
        except struct.error as e:
            self.close()
            raise ValueError(f"Failed to read header: {e}") from None

        self.edges = np.linspace(self.energyLow, self.energyHigh, self.nEnergyBins + 1)
        self.bin_centers = (self.edges[:-1] + self.edges[1:]) / 2.0

        expected_size = HEADER_SIZE + self.nChannels * self.nEnergyBins * 8
        if file_size < expected_size:
            self.close()
            raise ValueError(
                f"SHM too small: {file_size} bytes, expected {expected_size} "
                f"for {self.nChannels} channels x {self.nEnergyBins} bins"
            )

        print(f"ShmReader: {self.nChannels} channels, {self.nEnergyBins} bins, "
            f"energy [{self.energyLow}, {self.energyHigh}]")

 
    def read_all(self):
        t0 = time.time()
        #"""Read the full 2D array: shape (nChannels, nEnergyBins)"""
        data = np.frombuffer(
            self.buf, dtype=np.uint64,
            count=self.nChannels * self.nEnergyBins,
            offset=HEADER_SIZE
        ).reshape(self.nChannels, self.nEnergyBins)
        print(f"Reading the data took {(time.time() - t0)*1000:.1f} ms")
        return data
 
    def read_channel(self, ch):
        #"""Read a single channel spectrum"""
        if ch >= self.nChannels:
            raise ValueError(f"Channel {ch} out of range (max {self.nChannels - 1})")
        offset = HEADER_SIZE + ch * self.nEnergyBins * 8
        return np.frombuffer(
            self.buf, dtype=np.uint64,
            count=self.nEnergyBins,
            offset=offset
        ).copy()
 
    def read_channels(self, channels):
        #"""Read and sum multiple channel spectra"""
        total = np.zeros(self.nEnergyBins, dtype=np.uint64)
        for ch in channels:
            total += self.read_channel(ch)
        return total
 
    def read_energy_for_all_channels(self):
        t0 = time.time()
        data = self.read_all()
        energy = data.sum(axis=0)
        print(f"read_energy_for_all_channels took {(time.time() - t0)*1000:.1f} ms")
        return energy
    
    def read_energy_for_channel_range(self, minChannel, maxChannel):
        t0 = time.time()
        nChannelsInRange = maxChannel - minChannel + 1
        offset = HEADER_SIZE + minChannel * self.nEnergyBins * 8
        data = np.frombuffer(
        self.buf, dtype=np.uint64,
        count=nChannelsInRange * self.nEnergyBins,
        offset=offset
        ).reshape(nChannelsInRange, self.nEnergyBins)
        energy = data.sum(axis=0)
        print(f"read_energy_for_channel_range took {(time.time() - t0)*1000:.1f} ms")
        return energy
    
    def read_counts_for_all_channels(self):
        t0 = time.time()
        data = self.read_all() 
        
        counts = data.sum(axis=1)  

        nonzero_mask = counts != 0
        nonzero_indices = np.where(nonzero_mask)[0]
        
        first_nonzero = int(nonzero_indices[0])
        last_nonzero  = int(nonzero_indices[-1])
        print(f"read counts took {(time.time()-t0)*1000:.1f} ms")
        return first_nonzero, last_nonzero, counts
    
    def close(self):
        if self.buf:
            self.buf.close()
        if self.fd:
            os.close(self.fd)
 
    def __del__(self):
        self.close()
 
    def __enter__(self):
        return self
 
    def __exit__(self, *args):
        self.close()



