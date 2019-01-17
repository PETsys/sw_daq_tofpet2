#ifndef __PETSYS__EVENT_DECODE_HPP__DEFINED__
#define __PETSYS__EVENT_DECODE_HPP__DEFINED__

#include <stdint.h>
#include <string>

namespace PETSYS {       

class RawEventWord{

public:
	RawEventWord(uint64_t word) : word(word){};
	~RawEventWord() {};
	
	unsigned getEFine() {
		unsigned v = word % 1024;
		v = (v + 27) % 1024;	// rd_clk_en
		return v;
	};

	unsigned getTFine() {
		unsigned v = (word>>10) % 1024;
		v = (v + 27) % 1024;	// rd_clk_en
		return v;
	};

	unsigned getECoarse() {
		return (word>>20) % 1024;
	};
	
	unsigned getTCoarse() {
		return (word>>30) % 1024;
	};
	
	unsigned getTacID() {
	
		return (word>>40) % 4;
	};
	
	unsigned getChannelID() {
	
		return word>>42;
	};
	
private:
	uint64_t word;

};

}
#endif
