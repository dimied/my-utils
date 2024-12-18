
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define IS_WINDOWS 1
#endif

#ifdef _WIN64
#ifndef IS_WINDOWS
#define IS_WINDOWS 1
#endif
#endif

#ifdef __CYGWIN__
#ifndef IS_WINDOWS
#define IS_WINDOWS 1
#endif
#endif

#ifndef IS_WINDOWS
#define IS_WINDOWS 0
#endif

#define IS_NOT_WINDOWS (1 - IS_WINDOWS)

#if IS_NOT_WINDOWS
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define DO_PARAMETER_CHECKS 0

typedef struct _FileDesc
{
	FILE *pFile;
	char *pFilename;
} FileDesc;

typedef struct _Files
{
	FileDesc input;
	FileDesc output;
} Files;

// Used to save line information for duplicates
typedef struct _LineInfo
{
	unsigned int hash;
	int lineNr;
	int len;
	int offset;
	char command; // keep or skip
} LineInfo;

#if 0
int cmpLineInfo(const void *pA, const void *pB)
{
	LineInfo *pFirst = (LineInfo *)pA;
	LineInfo *pSecond = (LineInfo *)pB;

	if (pFirst->len == 0)
	{
		return pSecond->len == 0 ? 0 : +1;
	}
	if (pSecond->len == 0)
	{
		return -1;
	}
	if (pFirst->hash < pSecond->hash)
	{
		return -1;
	}
	if (pFirst->hash > pSecond->hash)
	{
		return 1;
	}

	int res = pFirst->lineNr - pSecond->lineNr;
	if (res < 0)
	{
		return -1;
	}
	return res > 0 ? 1 : 0;
}
#endif

unsigned int adler32(const void *buf, size_t buflength)
{
	const unsigned char *buffer = (const unsigned char *)buf;

	unsigned int s1 = 1, s2 = 0;

	for (size_t idx = 0; idx < buflength; idx++)
	{
		s1 = (s1 + buffer[idx]) % 65521;
		s2 = (s2 + s1) % 65521;
	}
	return (s2 << 16) | s1;
}

void closeAndNullify(FILE **ppFile)
{
	if (ppFile != 0 && *ppFile != 0)
	{
		fclose(*ppFile);
		*ppFile = 0;
	}
}

void closeFiles(Files *pFiles)
{
#if DO_PARAMETER_CHECKS
	if (pFiles == 0)
	{
		return;
	}
#endif
	closeAndNullify(&pFiles->input.pFile);
	closeAndNullify(&pFiles->output.pFile);
}

FILE *openFile(FileDesc *pDesc, char *pMode)
{
#if DO_PARAMETER_CHECKS
	if (pDesc == 0 || pMode == 0)
	{
		return 0;
	}
#endif
	pDesc->pFile = fopen(pDesc->pFilename, pMode);
	return pDesc->pFile;
}

int printError(Files *pFiles, const char *pError, char *pErrorParam)
{
	closeFiles(pFiles);
	printf(pError, pErrorParam);
	return 1;
}

LineInfo *collectLineInfos(char *pFileData, int numBytes, int *pNumLines,
						   int *pTrim)
{
	int byteOffset = 0, numLines = 0;
	char *pData = pFileData;
	int trimmingEnabled = 0;
	if (pTrim)
	{
		trimmingEnabled = 1;
		printf("Trimming enabled!\n");
	}

	while (byteOffset < numBytes)
	{
		if (*pData == '\n')
		{
			++numLines;
		}
		++pData;
		++byteOffset;
	}
	*pNumLines = numLines;

	size_t allocSize = numLines * sizeof(LineInfo);
	LineInfo *pLineInfos = (LineInfo *)malloc(allocSize);
	if (0 == pLineInfos)
	{
		return 0;
	}

	memset(pLineInfos, 0, allocSize);

	int lastByteOffset = 0, lineNr = 0, numEmptyLines = 0;
	int maxLength = 0, lineLen, numTrimed = 0;
	byteOffset = 0;
	pData = pFileData;

	while (byteOffset < numBytes)
	{
		if ('\n' == *pData)
		{
			pLineInfos[lineNr].lineNr = lineNr;
			pLineInfos[lineNr].offset = lastByteOffset;

			lineLen = byteOffset - lastByteOffset;

			if (trimmingEnabled && lineLen > 0 && byteOffset > 0)
			{
				int trimOffset = lineLen - 1;
				char *pTrimPtr = pFileData + byteOffset + trimOffset;

				while (trimOffset > 0 && *pTrimPtr != ' ' && *pTrimPtr != '\t')
				{
					--trimOffset;
					--pTrimPtr;
					++numTrimed;
				}
			}

			pLineInfos[lineNr].len = lineLen;
			numEmptyLines += (lineLen == 0) ? 1 : 0;
			if (maxLength < lineLen)
			{
				maxLength = lineLen;
			}
			unsigned int hash = 0;
			if (lineLen > 0)
			{
				char *p = pFileData + lastByteOffset;
				hash = adler32(p, lineLen);
			}
			pLineInfos[lineNr].hash = hash;
			lastByteOffset = byteOffset + 1;
			++lineNr;
		}
		++pData;
		++byteOffset;
	}
	if (lineNr < numLines)
	{
		printf("Last line\n");
		pLineInfos[lineNr].lineNr = lineNr;
		pLineInfos[lineNr].offset = lastByteOffset;

		lineLen = byteOffset - lastByteOffset;

		pLineInfos[lineNr].len = lineLen;
		numEmptyLines += (lineLen == 0) ? 1 : 0;
		if (maxLength < lineLen)
		{
			maxLength = lineLen;
		}
		unsigned int hash = 0;
		if (lineLen > 0)
		{
			char *p = pFileData + lastByteOffset;
			hash = adler32(p, lineLen);
		}
		pLineInfos[lineNr].hash = hash;
	}

	if (pTrim)
	{
		*pTrim = numTrimed;
	}

	return pLineInfos;
}

void markLines(LineInfo *pLineInfos, int numLines, char *pFileData)
{
	long int numChecks = 0, numSkipped = 0, numDuplicates = 0;
	unsigned int firstLineLen, hash, firstLineOffset;
	unsigned int secondLineLen, secondHash;
	char *pFirstLine, *pSecondLine;

	for (int firstLineIdx = 0; firstLineIdx < numLines; firstLineIdx++)
	{
		firstLineLen = pLineInfos[firstLineIdx].len;
		hash = pLineInfos[firstLineIdx].hash;
		if (0 == firstLineLen || 0 == hash)
		{
			++numSkipped;
			continue;
		}
		firstLineOffset = pLineInfos[firstLineIdx].offset;
		pFirstLine = pFileData + firstLineOffset;

		for (int secondLineIdx = firstLineIdx + 1; secondLineIdx < numLines;
			 secondLineIdx++)
		{
			secondLineLen = pLineInfos[secondLineIdx].len;
			secondHash = pLineInfos[secondLineIdx].hash;
			if (firstLineLen != secondLineLen || (hash != secondHash) ||
				pLineInfos[secondLineIdx].command > 0)
			{
				++numSkipped;
				continue;
			}

			++numChecks;
			pSecondLine = pFileData + pLineInfos[secondLineIdx].offset;

			if (0 == memcmp(pFirstLine, pSecondLine, firstLineLen))
			{
				++pLineInfos[secondLineIdx].command;
				++numDuplicates;
			}
		}
	}
	const char *pF = "#checks: %li | #skipped: %li | #dups: %li\n";
	printf(pF, numChecks, numSkipped, numDuplicates);
}

const char *psInputOutput = "Input: %s\nOutput: %s\n";
const char *psEqualNames =
	"No action: Input & output files have same name (%s)\n";
#if IS_NOT_WINDOWS
const char *psOutputStatError = "stat error for output file(%i): %s/n";
const char *psEqualInodes = "No action: Input & output files are the same file "
							"(have same inodes) (%s)\n";
#endif
const char *psErrorOpenInput = "ERROR: Can't open input file (%s)\n";
const char *psErrorOpenOutput = "ERROR: Can't open output file (%s)\n";
const char *psErrorSeek = "ERROR: Failed to seek in input file (%s).\n";
const char *psInputLengthErr = "Input file has less tha 2 bytes (%s)\n";
const char *psMemoryAllocErr = "ERROR: Failed to allocate memory for input file (%s)\n";
const char *psInputReadErr = "Failed to read file data got %li bytes != %li expected (ERROR:%i)\n";
const char* psLineMemoryAllocErr= "ERROR: Failed to allocate memory for lines. %s\n";
const char* psOutputWriteErr = "Failed to write line: %i. Bytes written/expected: %i/%i";

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		printf("ERROR: Missing parameters: duplines inputfile outputfile\n");
		return 1;
	}

	Files myFiles = {{0, argv[1]}, {0, argv[2]}};
	printf("Executable: %s\n", argv[0]);
	printf(psInputOutput, myFiles.input.pFilename, myFiles.output.pFilename);

	if (0 == strcmp(myFiles.input.pFilename, myFiles.output.pFilename))
	{
		printf(psEqualNames, myFiles.input.pFilename);
		return 1;
	}

#if IS_NOT_WINDOWS
	{
		struct stat inputStatInfo;
		struct stat outputStatInfo;

		if (stat(myFiles.input.pFilename, &inputStatInfo) == -1)
		{
			printf("stat error for input file: %s/n", myFiles.input.pFilename);
			exit(1);
			return 1;
		}
		int outputStat = stat(myFiles.output.pFilename, &outputStatInfo);
		if (outputStat == -1 && errno != ENOENT)
		{
			printf(psOutputStatError, errno, myFiles.output.pFilename);
			exit(1);
			return 1;
		}
		if (inputStatInfo.st_ino == outputStatInfo.st_ino)
		{
			printf(psEqualInodes, myFiles.input.pFilename);
			return 1;
		}
	}
#endif

	if (0 == openFile(&myFiles.input, "rb"))
	{
		return printError(&myFiles, psErrorOpenInput, myFiles.input.pFilename);
	}
	if (0 == openFile(&myFiles.output, "wb"))
	{
		return printError(&myFiles, psErrorOpenOutput, myFiles.output.pFilename);
	}
	if (0 != fseek(myFiles.input.pFile, 0, SEEK_END))
	{
		return printError(&myFiles, psErrorSeek, myFiles.input.pFilename);
	}
	long pos = ftell(myFiles.input.pFile);

	if (pos < 2)
	{
		rewind(myFiles.input.pFile);
		// printf("Need to check length (%s)\n", myFiles.input.pFilename);
		pos = 0;
		while (fgetc(myFiles.input.pFile) != EOF)
		{
			pos++;
		};
		if (ferror(myFiles.input.pFile))
		{
			puts("I/O error when reading");
			return 1;
		}
	}
	rewind(myFiles.input.pFile);
	printf("Size:input: %li bytes\n", pos);

	if (pos < 2)
	{
		return printError(&myFiles, psInputLengthErr, myFiles.input.pFilename);
	}
	char *pFileData = (char *)malloc(pos + 1);
	if (0 == pFileData)
	{
		return printError(&myFiles, psMemoryAllocErr, myFiles.input.pFilename);
	}
	memset(pFileData, 0, pos + 1);

	const size_t numBytes = fread(pFileData, 1, pos, myFiles.input.pFile);
	if (numBytes != pos)
	{
		if (feof(myFiles.input.pFile))
		{
			printf("Error: unexpected end of file\n");
		}

		int err = ferror(myFiles.input.pFile);
		printf(psInputReadErr, numBytes, pos, err);

		free(pFileData);
		closeFiles(&myFiles);
		return 1;
	}

	int numLines = 0;
	int numTrimmed = 0;

	LineInfo *pLineInfos =
		collectLineInfos(pFileData, numBytes, &numLines, &numTrimmed);

	
	if (0 == pLineInfos)
	{
		free(pFileData);
		return printError(&myFiles, psLineMemoryAllocErr,"");
	}
	printf("#lines: %i | #trimmed: %i\n", numLines, numTrimmed);

	markLines(pLineInfos, numLines, pFileData);

	FILE *fp = myFiles.output.pFile;
	int lastLen = 0;
	// TODO: Print to file
	for (int lineNr = 0; lineNr < numLines; lineNr++)
	{
		if (pLineInfos[lineNr].command > 0)
		{
			continue;
		}

		int lenToWrite = pLineInfos[lineNr].len;
		if (lastLen == 0 && lenToWrite == 0)
		{
			continue;
		}
		
		int written =
			fwrite(pFileData + pLineInfos[lineNr].offset, 1, lenToWrite, fp);
		if (lenToWrite != written || EOF == fputc('\n', fp))
		{
			printf(psOutputWriteErr, lineNr,written, lenToWrite);
			printf("Abort!");
			break;
		}
		lastLen = lenToWrite;
	}

	free(pFileData);
	free(pLineInfos);
	closeFiles(&myFiles);
	return 0;
}
