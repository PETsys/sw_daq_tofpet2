import os
import mmap
import struct
import numpy as np

HEADER_FORMAT = "=IIHHIddd8Q"
HEADER_SIZE   = struct.calcsize(HEADER_FORMAT)   # 104 bytes

ELAPSED_TIME_FORMAT = "=d"
ELAPSED_TIME_OFFSET = 32
ELAPSED_TIME_SIZE   = struct.calcsize(ELAPSED_TIME_FORMAT)

N_HIT_COUNTERS      = 4                   # fixed size of hitsSeen / hitsSampled in C
HITS_SEEN_OFFSET    = 40                    # right after elapsedTime
HITS_SAMPLED_OFFSET = HITS_SEEN_OFFSET + N_HIT_COUNTERS * 8
HITS_FORMAT         = "=4Q"

COUNTS_OFFSET   = HEADER_SIZE               # 104
COUNTS_DTYPE    = np.uint32
COUNTS_ITEMSIZE = 4


class MonitoringShmReader:
    def __init__(self, shm_name="/online_data"):
        self.fd  = None
        self.buf = None
        self.shm_name = shm_name

        shm_path = f"/dev/shm{shm_name}"

        try:
            self.fd   = os.open(shm_path, os.O_RDONLY)
            file_size = os.fstat(self.fd).st_size
            self.buf  = mmap.mmap(self.fd, file_size, access=mmap.ACCESS_READ)
        except OSError as e:
            self.close()
            raise OSError(f"Failed to open shared memory '{shm_path}': {e}") from None

        try:
            fields = struct.unpack_from(HEADER_FORMAT, self.buf, 0)
        except struct.error as e:
            self.close()
            raise ValueError(f"Failed to read header: {e}") from None

        (self.nChannels, self.nEnergyBins, self.nTypes,
         _reserved1, _reserved2,
         self.energyLow, self.energyHigh, _elapsedTime) = fields[:8]

        self.edges       = np.linspace(self.energyLow, self.energyHigh, self.nEnergyBins + 1)
        self.bin_centers = (self.edges[:-1] + self.edges[1:]) / 2.0
        self.bin_width   = (self.energyHigh - self.energyLow) / self.nEnergyBins

        expected_size = COUNTS_OFFSET + self.nTypes * self.nChannels * self.nEnergyBins * COUNTS_ITEMSIZE
        if file_size < expected_size:
            self.close()
            raise ValueError(
                f"SHM too small: {file_size} bytes, expected {expected_size} "
                f"for {self.nTypes} types x {self.nChannels} channels x {self.nEnergyBins} bins"
            )

    @staticmethod
    def is_valid(shm_name="/online_data"):
        shm_path = f"/dev/shm{shm_name}"
        if not os.path.exists(shm_path):
            return False

        fd = None
        try:
            fd = os.open(shm_path, os.O_RDONLY)
            file_size = os.fstat(fd).st_size

            if file_size < HEADER_SIZE:
                return False

            with mmap.mmap(fd, HEADER_SIZE, access=mmap.ACCESS_READ) as buf:
                try:
                    fields = struct.unpack_from(HEADER_FORMAT, buf, 0)
                except struct.error:
                    return False

            nChannels, nEnergyBins, nTypes = fields[0], fields[1], fields[2]
            energyLow, energyHigh = fields[5], fields[6]

            if nChannels == 0 or nEnergyBins == 0 or nTypes == 0:
                return False
            if not (energyHigh > energyLow):
                return False

            expected_size = COUNTS_OFFSET + nTypes * nChannels * nEnergyBins * COUNTS_ITEMSIZE
            if file_size < expected_size:
                return False

            return True

        except OSError:
            return False
        finally:
            if fd is not None:
                os.close(fd)

    def read_elapsed_time(self):
        return struct.unpack_from(ELAPSED_TIME_FORMAT, self.buf, ELAPSED_TIME_OFFSET)[0]

    def read_hit_counters(self, dataType):
        if dataType < 0 or dataType >= N_HIT_COUNTERS:
            return 0, 0
        seen    = struct.unpack_from(HITS_FORMAT, self.buf, HITS_SEEN_OFFSET)[dataType]
        sampled = struct.unpack_from(HITS_FORMAT, self.buf, HITS_SAMPLED_OFFSET)[dataType]
        return seen, sampled

    def scale_factor(self, dataType):
        seen, sampled = self.read_hit_counters(dataType)
        if sampled == 0:
            return 1.0
        return seen / sampled

    def read_all(self):
        return np.frombuffer(
            self.buf, dtype=COUNTS_DTYPE,
            count=self.nTypes * self.nChannels * self.nEnergyBins,
            offset=COUNTS_OFFSET
        ).reshape(self.nTypes, self.nChannels, self.nEnergyBins)

    def read_type(self, dataType):
        offset = COUNTS_OFFSET + dataType * self.nChannels * self.nEnergyBins * COUNTS_ITEMSIZE
        return np.frombuffer(
            self.buf, dtype=COUNTS_DTYPE,
            count=self.nChannels * self.nEnergyBins,
            offset=offset
        ).reshape(self.nChannels, self.nEnergyBins)

    def read_energy_for_all_channels(self, dataType):
        data = self.read_type(dataType)
        energy = data.sum(axis=0, dtype=np.uint64) * self.scale_factor(dataType)
        return np.rint(energy).astype(np.uint64)

    def read_energy_for_channel_range(self, dataType, minChannel, maxChannel):
        minChannel = max(0, min(minChannel, self.nChannels - 1))
        maxChannel = max(0, min(maxChannel, self.nChannels - 1))

        if minChannel > maxChannel:
            return None, None, np.zeros(self.nEnergyBins, dtype=np.uint64)

        nChannelsInRange = maxChannel - minChannel + 1
        offset = COUNTS_OFFSET + (dataType * self.nChannels + minChannel) * self.nEnergyBins * COUNTS_ITEMSIZE

        expected_end = offset + nChannelsInRange * self.nEnergyBins * COUNTS_ITEMSIZE
        if expected_end > len(self.buf):
            return None, None, np.zeros(self.nEnergyBins, dtype=np.uint64)

        data = np.frombuffer(
            self.buf, dtype=COUNTS_DTYPE,
            count=nChannelsInRange * self.nEnergyBins,
            offset=offset
        ).reshape(nChannelsInRange, self.nEnergyBins)

        energy = data.sum(axis=0, dtype=np.uint64) * self.scale_factor(dataType)
        energy = np.rint(energy).astype(np.uint64)

        nonzero_indices = np.where(energy != 0)[0]
        if nonzero_indices.size == 0:
            return None, None, energy

        first_nonzero_energy = float(self.bin_centers[nonzero_indices[0]])
        last_nonzero_energy  = float(self.bin_centers[nonzero_indices[-1]])
        return first_nonzero_energy, last_nonzero_energy, energy

    def read_counts_for_all_channels(self, dataType):
        data = self.read_type(dataType)
        counts = data.sum(axis=1, dtype=np.uint64) * self.scale_factor(dataType)
        counts = np.rint(counts).astype(np.uint64)

        nonzero_indices = np.where(counts != 0)[0]
        if nonzero_indices.size == 0:
            return 0, 1024, counts

        return int(nonzero_indices[0]), int(nonzero_indices[-1]), counts

    def close(self):
        if self.buf is not None:
            self.buf.close()
            self.buf = None
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __del__(self):
        self.close()
