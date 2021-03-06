#define _GNU_SOURCE

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "htslib/sam.h"

#include "include/bamdb.h"
#include "include/bam_sqlite.h"
#include "include/bam_api.h"

/* I really hope we don't have sequences longer than this */
#define WORK_BUFFER_SIZE 65536

#define get_int_chars(i) ((i == 0) ? 1 : floor(log10(abs(i))) + 1)


static void
get_bam_tags(const bam1_t *row, char *buffer)
{
	/* Output as TAG:TYPE:VALUE
	 * TAG is two characters
	 * TYPE is a single character
	 * Example: QT:Z:AAFFFKKK */
	uint8_t *aux;
	uint8_t key[2];
	uint8_t type, sub_type;
	size_t buffer_pos = 0;
	uint32_t arr_size;
	char *dummy = 0;

	aux = bam_get_aux(row);
	while (aux+4 <= row->data + row->l_data) {
		key[0] = aux[0];
		key[1] = aux[1];
		sprintf(buffer + buffer_pos, "%c%c:", key[0], key[1]);
		buffer_pos += 3;

		type = aux[2];
		aux += 3;

		/* TODO: add error handling for values that don't conform to type */
		switch(type) {
			case 'A': /* Printable character */
				sprintf(buffer + buffer_pos, "A:%c", *aux);
				buffer_pos += 3;
				aux++;
				break;
			case 'C': /*Signed integer */
				sprintf(buffer + buffer_pos, "i:%d", *aux);
				buffer_pos += 2 + get_int_chars(*aux);
				aux++;
				break;
			case 'c':
				sprintf(buffer + buffer_pos, "i:%" PRId8, *(int8_t*)aux);
				buffer_pos += 2 + get_int_chars(*aux);
				aux++;
				break;
			case 'S':
				sprintf(buffer + buffer_pos, "i:%" PRIu16, *(uint16_t*)aux);
				buffer_pos += 2 + get_int_chars(*aux);
				aux += 2;
				break;
			case 's':
				sprintf(buffer + buffer_pos, "i:%" PRId16, *(int16_t*)aux);
				buffer_pos += 2 + get_int_chars(*aux);
				aux += 2;
				break;
			case 'I':
				sprintf(buffer + buffer_pos, "i:%" PRIu32, *(uint32_t*)aux);
				buffer_pos += 2 + get_int_chars(*aux);
				aux += 4;
				break;
			case 'i':
				sprintf(buffer + buffer_pos, "i:%" PRId32, *(int32_t*)aux);
				buffer_pos += 2 + get_int_chars(*aux);
				aux += 4;
				break;
			case 'f': /* Single precision floating point */
				sprintf(buffer + buffer_pos, "f:%g", *(float*)aux);
				/* Figure out how many chars the fp takes as a string */
				buffer_pos += 2 + snprintf(dummy, 0, "%g", *(float*)aux);
				aux += 4;
				break;
			case 'd':
				/* Double precision floating point. This does appear to be in the BAM spec,
				 * I'm copying from samtools which does provide for this */
				sprintf(buffer + buffer_pos, "d:%g", *(float*)aux);
				/* Figure out how many chars the fp takes as a string */
				buffer_pos += 2 + snprintf(dummy, 0, "%g", *(float*)aux);
				aux += 4;
				break;
			case 'Z': /* Printable string */
			case 'H': /* Byte array */
				sprintf(buffer + buffer_pos, "%c:", type);
				buffer_pos += 2;
				while (aux < row->data + row->l_data && *aux) {
					sprintf(buffer + buffer_pos, "%c", *aux++);
					buffer_pos++;
				}
				aux++;
				break;
			case 'B': /* Integer or numeric array */
				sub_type = *(aux++);
				memcpy(&arr_size, aux, 4);

				sprintf(buffer + buffer_pos, "B:%c", sub_type);
				buffer_pos += 3;
				for (int i = 0; i < arr_size; ++i) {
					sprintf(buffer + buffer_pos, ",");
					buffer_pos++;
					switch (sub_type) {
						case 'c':
							sprintf(buffer + buffer_pos, "%d", *aux);
							buffer_pos += get_int_chars(*aux);
							aux++;
							break;
						case 'C':
							sprintf(buffer + buffer_pos, "%" PRId8, *(int8_t*)aux);
							buffer_pos += get_int_chars(*aux);
							aux++;
							break;
						case 'S':
							sprintf(buffer + buffer_pos, "%" PRIu16, *(uint16_t*)aux);
							buffer_pos += get_int_chars(*aux);
							aux += 2;
							break;
						case 's':
							sprintf(buffer + buffer_pos, "%" PRId16, *(int16_t*)aux);
							buffer_pos += get_int_chars(*aux);
							aux += 2;
							break;
						case 'I':
							sprintf(buffer + buffer_pos, "i:%" PRIu32, *(uint32_t*)aux);
							buffer_pos += 2 + get_int_chars(*aux);
							aux += 4;
							break;
						case 'i':
							sprintf(buffer + buffer_pos, "i:%" PRId32, *(int32_t*)aux);
							buffer_pos += 2 + get_int_chars(*aux);
							aux += 4;
							break;
						case 'f': /* Single precision floating point */
							sprintf(buffer + buffer_pos, "f:%g", *(float*)aux);
							/* Figure out how many chars the fp takes as a string */
							buffer_pos += 2 + snprintf(dummy, 0, "%g", *(float*)aux);
							aux += 4;
							break;
					}
				}
				break;
		}

		sprintf(buffer + buffer_pos, "\t");
		buffer_pos++;
	}
}


static int
print_bam_row(const bam1_t *row, const bam_hdr_t *header, char *work_buffer)
{
	static uint32_t rows = 0;
	char *temp;

	printf("Row %u:\n", rows);
	printf("\tQNAME: %s\n", bam_get_qname(row));
	printf("\tFLAG: %u\n", row->core.flag);
	printf("\tRNAME: %s\n", bam_get_rname(row, header));
	printf("\tPOS: %d\n", row->core.pos);
	printf("\tMAPQ: %u\n", row->core.qual);

	temp = work_buffer;
	printf("\tCIGAR: %s\n", bam_cigar_str(row, work_buffer));
	work_buffer = temp;

	printf("\tRNEXT: %s\n", bam_get_rnext(row, header));
	printf("\tPNEXT: %d\n", row->core.mpos + 1);
	printf("\tTLEN: %d\n", row->core.isize);

	temp = work_buffer;
	printf("\tSEQ: %s\n", bam_seq_str(row, work_buffer));
	work_buffer = temp;

	temp = work_buffer;
	printf("\tQUAL: %s\n", bam_qual_str(row, work_buffer));
	work_buffer = temp;

	temp = work_buffer;
	printf("\tBX: %s\n", bam_bx_str(row, work_buffer));
	work_buffer = temp;

	/* TAGs */
	get_bam_tags(row, work_buffer);
	printf("\tTAGs: %s\n", work_buffer);

	rows++;
	return 0;
}


static int
read_file(samFile *input_file)
{
	bam_hdr_t *header = NULL;
	bam1_t *bam_row;
	char *work_buffer;
	int r = 0;
	int rc = 0;

	header = sam_hdr_read(input_file);
	if (header == NULL) {
		fprintf(stderr, "Unable to read the header from %s\n", input_file->fn);
		rc = 1;
		goto exit;
	}

	bam_row = bam_init1();
	work_buffer = malloc(WORK_BUFFER_SIZE);
	while ((r = sam_read1(input_file, header, bam_row)) >= 0) { // read one alignment from `in'
		print_bam_row(bam_row, header, work_buffer);
	}
	if (r < -1) {
		fprintf(stderr, "Attempting to process truncated file.\n");
		rc = 1;
		goto exit;
	}

exit:
	free(work_buffer);
	bam_destroy1(bam_row);
	return rc;
}


int
main(int argc, char *argv[]) {
	int rc = 0;
	int c;
	samFile *input_file = 0;
	bam_args_t bam_args;

	bam_args.convert_to = BAMDB_CONVERT_TO_TEXT;
	while ((c = getopt(argc, argv, "t:f:")) != -1) {
			switch(c) {
				case 't':
					if (strcmp(optarg, "sqlite") == 0) {
						bam_args.convert_to = BAMDB_CONVERT_TO_SQLITE;
					} else if (strcmp(optarg, "text") == 0) {
						bam_args.convert_to = BAMDB_CONVERT_TO_TEXT;
					} else {
						fprintf(stderr, "Invalid output format %s\n", optarg);
						return 1;
					}
					break;
				case 'f':
					strcpy(bam_args.input_file_name, optarg);
					break;
				default:
					fprintf(stderr, "Unknown argument\n");
					return 1;
			}
	}

	/* Get filename from first non option argument */
	if (optind < argc) {
		strcpy(bam_args.input_file_name, argv[optind]);
	}

	if ((input_file = sam_open(bam_args.input_file_name, "r")) == 0) {
		fprintf(stderr, "Unable to open file %s\n", bam_args.input_file_name);
		return 1;
	}

	if (bam_args.convert_to == BAMDB_CONVERT_TO_SQLITE) {
		rc = convert_to_sqlite(input_file, NULL);
	} else {
		rc = read_file(input_file);
	}

	return rc;
}
