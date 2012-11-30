#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>

/* meta 1768 byte */
#define META_FILE_SIZE 0x6E8

/* ts packet 188 Byte */
#define TS_PACKET_SIZE 188

#define SWAP_ENDIAN(val) ((int) ( \
	(((val) & 0x000000ff) << 24) | \
	(((val) & 0x0000ff00) <<  8) | \
	(((val) & 0x00ff0000) >>  8) | \
	(((val) & 0xff000000) >> 24) ))

#define TO_MS(h, m, s, ms) ((int) (1000 * (60 * ((60 * h) + m) + s) + ms))

/* prototype */
unsigned long get_PCR_both_ends(FILE* fp, char* str);
void get_duration_str(unsigned long, char*);
static int charset_convert(const char*, const char*, const char*, size_t, char*, size_t);

void main(int argc, char *argv[])
{
	char buf[BUFSIZ];
	unsigned int ret;

	// titles.csv のカラム
	char id[12];
	char date[19];
	unsigned int def; // 1 = High Definition, 2 = Standard
	char title[128];

	// title 文字コード変換
	char title_e[128];
	char title_s[128];

	// *.ts ファイルの再生時間
	unsigned long duration;
	char duration_str[12];

	// 引数 <ts_directory> がない場合、終了
	if(argc < 2){
		fprintf(stderr, "Usage: foo.exe <ts_directory>\n\n");
		exit(EXIT_FAILURE);
	}

	// argv[1] = <ts_directory> 内の(フルパス)ファイル名(*.ts, *.ts.meta, *.ts.chap)
	char ts[strlen(argv[1]) + sizeof(id) + 3]; // *.ts
	char meta[sizeof(id) + 3 + 5];             // *.ts.meta
	char chap[sizeof(id) + 3 + 5];             // *.ts.chap

	FILE* csvfp;
	FILE* tsfp;
	FILE* metafp;
	FILE* chapfp;

	// .exe と同じディレクトリの titles.csv(UTF-8) を開く。開けない場合、終了
	// "000","0000/00/00 00_00_00",1,"title"
	char csv[128];
	sprintf(csv, "%s\\titles.csv", dirname(argv[0]));
	if((csvfp = fopen(csv, "r")) == NULL){
		printf("Can't open [%s].\n", csv);
		exit(EXIT_FAILURE);
	}
	printf("%s is found.\n", csv);

	while ((NULL != fgets(buf, BUFSIZ, csvfp))) {
		ret = sscanf(buf, "\"%[^,\"]\",\"%[^,\"]\",%d,\"%[^,\"]\"", id, date, &def, title);
		if(ret != 4) continue;

		// stdout 用(SHIFT_JIS)
		charset_convert("UTF-8", "SHIFT_JIS", title, sizeof(title), title_s, sizeof(title_s));
		// *.meta 用(EUC-JP)
		charset_convert("UTF-8", "EUC-JP", title, sizeof(title), title_e, sizeof(title_e));

		strcpy(ts, argv[1]); strcat(ts, id); strcat(ts, ".ts");
		strcpy(meta, id); strcat(meta, ".ts.meta");
		strcpy(chap, id); strcat(chap, ".ts.chap");

		if((tsfp = fopen(ts, "rb")) == NULL){
			printf("- %s; %s; %d; \n  %s;\n", id, date, def, title_s);
			continue;
		}

		duration = get_PCR_both_ends(tsfp, duration_str);
		printf("* %s; %s; %d; > %8d = %s\n  %s;\n", id, date, def, duration, duration_str, title_s);

		if((metafp = fopen(meta, "wb")) == NULL){
			printf("- Can NOT open %s (*.ts.meta)\n", meta);
			continue;
		}
		write_meta(metafp, date, def, duration_str, title_e);
		fclose(metafp);

		if((chapfp = fopen(chap, "wb")) == NULL){
			printf("- Can NOT open %s (*.ts.chap)\n", chap);
			continue;
		}
		write_chap(chapfp, duration);
		fclose(chapfp);

		fclose(tsfp);
	}

	fclose(csvfp);
}

int write_chap(FILE* fp, const int duration)
{
	unsigned int i;
	unsigned int duration_le; /* little endian */
	unsigned int type    = 0x02000000;
	unsigned int unknown = 0x00000000;
	/* 5 * 60 * 1000 = 300000 ms 毎にチャプタ *.chap を生成 */
	for(i = 0; i * 5 * 60 * 1000 < duration; i++){
		duration_le = SWAP_ENDIAN(i * 5 * 60 * 1000);
		fwrite(&duration_le, sizeof(duration_le), 1, fp);
		fwrite(&type, sizeof(type), 1, fp);
		fwrite(&unknown, sizeof(unknown), 1, fp);
	}
	return EXIT_SUCCESS;
}

int write_meta(FILE* fp, const char *date, const int def, const char* duration_str, const char *title)
{
	fseek(fp, 0x01B, SEEK_SET);
	fwrite(title, strlen(title), 1, fp);
	fseek(fp, 0x0EE, SEEK_SET);
	fwrite(date, strlen(date), 1, fp);
	fseek(fp, 0x10E, SEEK_SET);
	fwrite(duration_str, 8, 1, fp);
	fseek(fp, 0x12E, SEEK_SET);
	fwrite(date, strlen(date), 1, fp);
	fseek(fp, 0x14E, SEEK_SET);
	fwrite(duration_str, 8, 1, fp);
	fseek(fp, 0x16E, SEEK_SET);
	fwrite(duration_str, 8, 1, fp);
	//fseek(fp, 0x5E0, SEEK_SET);
	//fwrite(description, strlen(description), 1, fp);
	fseek(fp, 0x5B1, SEEK_SET);
	fwrite(&def, sizeof(def), 1, fp);
	fseek(fp, META_FILE_SIZE - 1, SEEK_SET);
	fputc(0, fp);
	return EXIT_SUCCESS;
}


unsigned long get_PCR_both_ends(FILE* fp, char* str)
{
	unsigned char buf[TS_PACKET_SIZE];
	unsigned long payload_start_index = 4;
	unsigned long j, i = 0, reverse = 0;
	unsigned long pcr;
	unsigned long first;
	char duration_str[12];
	fpos_t fpos;

	while(fread(buf, sizeof(buf), 1, fp)){
		if(reverse > 0){
			i++;
			fseeko(fp, -(i * sizeof(buf)), SEEK_END);
		}
		if((((buf[3] & 0b00110000) >> 4) & 0b10) && (buf[5] & 0b00010000)){
			pcr = ( buf[6]     << 25 |
					buf[6 + 1] << 17 |
					buf[6 + 2] <<  9 |
					buf[6 + 3] <<  1 |
					buf[6 + 4] >>  7 ) / 90;
			//printf("\t");
			//fgetpos(fp, &fpos);
			//printf("%010llX ", fpos - sizeof(buf));
			//get_duration_str(pcr, duration_str);
			//printf("%8ld %s ", pcr, duration_str);
			if(reverse){
				//get_duration_str(pcr - first, duration_str);
				//printf("%8ld %s ", pcr - first, duration_str);
			}
			//printf("\n");
			if(reverse == 1){
				sprintf(str, "%02lu_%02lu_%02lu",
					(pcr - first) / 1000 / 60 / 60,
					(pcr - first) / 1000 / 60 % 60,
					(pcr - first) / 1000 % 60
				);
				break;
			}else{
				first = pcr;
				reverse++;
			}
		}
	}
	return pcr - first;
}

void get_duration_str(unsigned long duration, char* str)
{
	sprintf(str, "%02lu:%02lu:%02lu.%03lu",
		duration / 1000 / 60 / 60,
		duration / 1000 / 60 % 60,
		duration / 1000 % 60,
		duration % 1000
	);
}

static int charset_convert(const char* from_charset, const char* to_charset, const char* src_buff, size_t src_size, char* dst_buff, size_t dst_size)
{
	iconv_t icd;
	char* inbuff;
	char* outbuff;
	size_t inbuff_size, outbuff_size;
	int ret;

	/* Charset を変換する descriptor を生成する */
	icd = iconv_open(to_charset, from_charset);
	if (icd < 0) {
		fprintf(stderr, "Error: iconv_open\n");
		return -1;
	}

	inbuff = (char*)src_buff;
	inbuff_size = src_size;
	outbuff = dst_buff;
	outbuff_size = dst_size;
	ret = iconv(icd, &inbuff, &inbuff_size, &outbuff, &outbuff_size);
	//printf("ret = %d inbuff_size = %d outbuff_size = %d\n", ret, inbuff_size, outbuff_size);
	iconv_close(icd);

	return (dst_size - outbuff_size);
}

	/*
	unsigned short int col;
	const char delim[] = ",\"";
	char* p;

	while ((NULL != fgets(buf, BUFSIZ, csvfp))) {
		col = 0;
		while(1){
			p = (col == 0) ? strtok(buf, delim): strtok(NULL, delim);
			if(p == NULL) break;
			if(col == 0 && strlen(p) < 3) break;
			printf("%s | ", p);
			col++;
		}
		if(col != 0) printf("\n");
		// ...
	}
	*/


