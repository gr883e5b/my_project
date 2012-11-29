#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[])
{
	// 080808080808080808080808080808080808080808080808080808080909090808080808080808080909090A09090909090A0A0A0A0A0C0C0C0C0E0E0F111115 
	// 08080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808
	
	if(argc < 2 || strlen(argv[1]) != 128){
		printf("No argument or invalid length (128)\n");
		 
		exit(EXIT_FAILURE);
	}
	unsigned int i;
	char* p;
	char buf[2];

	p = argv[1];
	printf("--cqm8i\n");
	printf("--cqm8p\n");
	for(i = 0; i < 64; i++, p += 2){
		strncpy(buf, p, 2);
		printf("%d", strtol(buf, (char **)NULL, 16));
		if(i < 64 - 1) printf(",");
	}
	printf("\n");

	p = argv[1];
	printf("INTRA8X8_LUMA =\n");
	printf("INTER8X8_LUMA =\n");
	for(i = 0; i < 64; i++, p += 2){
		strncpy(buf, p, 2);
		printf("%3d", strtol(buf, (char **)NULL, 16));
		if(i < 64 - 1) printf(",");
		if((i % 8) == 7) printf("\n");
	}
	return 0;
}
