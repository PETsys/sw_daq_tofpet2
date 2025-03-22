#ifndef __PETSYS_SYSTEMCONFIG_HPP__DEFINED__
#define __PETSYS_SYSTEMCONFIG_HPP__DEFINED__

#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <set>
#include <map>
#include <stdio.h>

namespace PETSYS
{

	class SystemConfig
	{
	public:
		static const u_int64_t LOAD_ALL = 0xFFFFFFFFFFFFFFFFULL;
		static const u_int64_t LOAD_SYSTEM_MAP = 0x0000000000000001ULL;
		static const u_int64_t LOAD_TDC_CALIBRATION = 0x0000000000000002ULL;
		static const u_int64_t LOAD_QDC_CALIBRATION = 0x0000000000000004ULL;
		static const u_int64_t LOAD_ENERGY_CALIBRATION = 0x0000000000000008ULL;
		static const u_int64_t LOAD_MAPPING = 0x0000000000000010ULL;
		static const u_int64_t LOAD_TIMEALIGN_CALIBRATION = 0x000000000000020ULL;
		static const u_int64_t LOAD_FIRMWARE_EMPIRICAL_CALIBRATIONS = 0x000000000000040ULL;

		struct TacConfig {
			float t0;
			float a0;
			float a1;
			float a2;
		};
		struct QacConfig {
			float p0;
			float p1;
			float p2;
			float p3;
			float p4;
			float p5;
			float p6;
			float p7;
			float p8;
			float p9;
		};
		struct FirmwareConfig {
			float p0;
			float p1;
			float p2;
			float k0;
			bool isValid;
		};
		struct EnergyConfig {
			float p0;
			float p1;
			float p2;
			float p3;
		};
		struct ChannelConfig {
			float x, y, z;
			int xi, yi;
			int triggerRegion;
			float t0;
			TacConfig tac_T[4];
			TacConfig tac_E[4];
			QacConfig qac_Q[4];
			FirmwareConfig empConfig[4];
			EnergyConfig eCal[4];
		};

		// Software trigger configuration
		int sw_trigger_group_max_hits;
		int sw_trigger_group_min_hits;
		float sw_trigger_group_min_energy;
		float sw_trigger_group_max_energy;

		float sw_fw_trigger_group_min_energy;
		float sw_fw_trigger_group_max_energy;
		int sw_fw_trigger_group_min_nhits;
		int sw_fw_trigger_group_max_nhits;
		float sw_fw_trigger_coinc_window;
		float sw_fw_trigger_pre_window;
		float sw_fw_trigger_post_window;

		float sw_trigger_group_max_distance;
		double sw_trigger_group_time_window;
		double sw_trigger_coincidence_time_window;

		static SystemConfig *fromFile(const char *configFileName);
		static SystemConfig *fromFile(const char *configFileName, u_int64_t mask);

		inline bool useTDCCalibration() { return hasTDCCalibration; };
		inline bool useQDCCalibration() { return hasQDCCalibration; };
		inline bool useEnergyCalibration() { return hasEnergyCalibration; };
		inline bool useFirmwareEmpiricalCalibrations() { return hasFirmwareEmpiricalCalibrations; };
		inline bool useTimeOffsetCalibration() { return hasTimeOffsetCalibration; };
		inline bool useXYZ() { return hasXYZ; };

		inline SystemConfig::ChannelConfig &getChannelConfig(unsigned channelID){
			unsigned indexH = channelID / 4096;
			unsigned indexL = channelID % 4096;

			ChannelConfig *ptr = channelConfig[indexH];
			if (ptr == NULL)
				return nullChannelConfig;
			else
				return ptr[indexL];
		};

		inline bool isCoincidenceAllowed(int r1, int r2) {
			if ((r1 < 0) || (r2 < 0)) return false;
			return coincidenceTriggerMap[r1 * MAX_TRIGGER_REGIONS + r2];
		};

		inline bool isMultiHitAllowed(int r1, int r2) {
			if ((r1 < 0) || (r2 < 0)) return false;
			return multihitTriggerMap[r1 * MAX_TRIGGER_REGIONS + r2];
		};

		std::map<unsigned, unsigned> mapTriggerRegions;
		std::map<unsigned, unsigned> mapTriggerRegionsInverted;

		SystemConfig();
		~SystemConfig();

	private:
		void touchChannelConfig(unsigned channelID);
		static bool areHwTriggerThresholdsDefault(SystemConfig *config);
		static void loadTDCCalibration(SystemConfig *config, const char *fn);
		static void loadQDCCalibration(SystemConfig *config, const char *fn);
		static void loadFirmwareEmpiricalCalibration(SystemConfig *config, const char *fn);
		static void makeSimpleFirmwareEmpiricalCalibration(SystemConfig *config, const char *fn);
		static void loadEnergyCalibration(SystemConfig *config, const char *fn);
		static void loadTimeOffsetCalibration(SystemConfig *config, const char *fn);
		static void loadChannelMap(SystemConfig *config, const char *fn);
		static void loadTriggerMap(SystemConfig *config, const char *fn);

		bool hasTDCCalibration;
		bool hasQDCCalibration;
		bool hasEnergyCalibration;
		bool hasFirmwareEmpiricalCalibrations;
		bool hasTimeOffsetCalibration;
		bool hasXYZ;

		ChannelConfig **channelConfig;
		ChannelConfig nullChannelConfig;

		static const unsigned MAX_TRIGGER_REGIONS = 4096; // 1024 FEB/D x 4 regions;

		bool *coincidenceTriggerMap;
		bool *multihitTriggerMap;
	};

}

#endif // __PETSYS_SYSTEMCONFIG_HPP__DEFINED__
