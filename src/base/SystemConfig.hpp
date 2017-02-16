#ifndef __PETSYS_SYSTEMCONFIG_HPP__DEFINED__
#define __PETSYS_SYSTEMCONFIG_HPP__DEFINED__

#include <stdlib.h>
#include <stdint.h>

namespace PETSYS {

	class SystemConfig {
	public:
		static const uint64_t LOAD_ALL			= 0xFFFFFFFFFFFFFFFFULL;
		static const uint64_t LOAD_SYSTEM_MAP		= 0x0000000000000001ULL;
		static const uint64_t LOAD_TDC_CALIBRATION	= 0x0000000000000002ULL;
		static const uint64_t LOAD_QDC_CALIBRATION	= 0x0000000000000004ULL;
		static const uint64_t LOAD_ENERGY_CALIBRATION	= 0x0000000000000006ULL;

		struct TacConfig {
			float t0;
			float tB;
			float m;
			float p2;
		};
		struct ChannelConfig {
			float x, y, z;
			int xi, yi;
			int triggerRegion;
			float t0;
			TacConfig tac_T[4];
			TacConfig tac_E[4];
		};

		static SystemConfig *fromFile(const char *configFileName);
		static SystemConfig *fromFile(const char *configFileName, uint64_t mask);
		
		SystemConfig::ChannelConfig &getChannelConfig(unsigned channelID) {
			unsigned indexH = channelID / 4096;
			unsigned indexL= channelID % 4096;
			
			ChannelConfig *ptr = channelConfig[indexH];
			if(ptr == NULL) 
				return nullChannelConfig;
			else
				return ptr[indexL];
		};

		SystemConfig();
		~SystemConfig();
		
	private:
		void touchChannelConfig(unsigned channelID);
		static void loadTDCCalibration(SystemConfig *config, const char *fn);
		
		ChannelConfig **channelConfig;
		ChannelConfig nullChannelConfig;
		
	};
	
	
	
}

#endif // __PETSYS_SYSTEMCONFIG_HPP__DEFINED__
