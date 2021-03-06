
#define MAX_FILENAME 1024

enum bamdb_convert_to {
	BAMDB_CONVERT_TO_TEXT,
	BAMDB_CONVERT_TO_SQLITE
};

typedef struct bamdb_args {
	enum bamdb_convert_to convert_to;
	char input_file_name[MAX_FILENAME];
} bam_args_t;
