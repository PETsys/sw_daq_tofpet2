#include <stdio.h>
#include <boost/lexical_cast.hpp>

int main(int argc, char *argv[])
{
	char fName[1024];

	char *outputFilenamePrefix = argv[1];
	sprintf(fName, "%s.rawf", outputFilenamePrefix);
	FILE *outputFileRaw = fopen(fName, "w");

	sprintf(fName, "%s.idxf", outputFilenamePrefix);
	FILE *outputFileIdx = fopen(fName, "w");

	int nInput = boost::lexical_cast<int>(argv[2]);

	for(int i = 0; i < nInput; i++) {
		char *inputFilenamePrefix = argv[i+3];
		sprintf(fName, "%s.rawf", inputFilenamePrefix);
		FILE *inputFileRaw = fopen(fName, "r");

		sprintf(fName, "%s.idxf", inputFilenamePrefix);
		FILE *inputFileIdx = fopen(fName, "r");


		unsigned long bs = 128*1024;
		char *buffer = new char[bs];

		fread(buffer, 8, 8, inputFileRaw);
		fwrite(buffer, 8, 8, outputFileRaw);

		unsigned long stepBegin, stepEnd;
		long long stepFirstFrame, stepLastFrame;
		float step1, step2;
		while(fscanf(inputFileIdx, "%lu\t%lu\t%lld\t%lld\t%f\t%f", &stepBegin, &stepEnd, &stepFirstFrame, &stepLastFrame, &step1, &step2) == 6) {
			unsigned long newStepBegin = ftell(outputFileRaw);

			fseek(inputFileRaw, stepBegin, SEEK_SET);
			unsigned long current = stepBegin;
			while(current < stepEnd) {
				unsigned long count = stepEnd - current;
				if(count > bs) count = bs;
				unsigned long r = fread(buffer, 1, count, inputFileRaw);
				fwrite(buffer, 1, r, outputFileRaw);
				current += r;
			}


			unsigned long newStepEnd = ftell(outputFileRaw);
			fprintf(outputFileIdx, "%lu\t%lu\t%lld\t%lld\t%f\t%f\n", newStepBegin, newStepEnd, stepFirstFrame, stepLastFrame, step1, step2);

		}

		fclose(inputFileRaw);
		fclose(inputFileIdx);

	}

	fclose(outputFileRaw);
	fclose(outputFileIdx);
}
