
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bam_sqlite.h"
#include "bam_api.h"


#define TABLE "CREATE TABLE IF NOT EXISTS seq (id INTEGER PRIMARY KEY, qname TEXT, bx STRING, bam_record STRING)"
#define WORK_BUFFER_SIZE 65536
#define SQL_BUFFER_SIZE 1024

static char *
get_default_dbname(const char *filename)
{
	/* Strip bam file extension and use .db */
	char *db_name;
	char *dot = strrchr(filename, '.');

	db_name = malloc(dot - filename + 3); /* Leak this */
	strncpy(db_name, filename, dot - filename);
	strcpy(db_name + (dot - filename), ".db");

	return db_name;
}


int
convert_to_sqlite(samFile *input_file, char *db_name)
{
	sqlite3 *db;
	sqlite3_stmt *stmt;
	char sql[SQL_BUFFER_SIZE];
	char *err_msg = 0;
	int rc, r;
	int ret = 0;

	bam_hdr_t *header = NULL;
	bam1_t *bam_row;
	char *work_buffer = malloc(WORK_BUFFER_SIZE);
	char *buffer_pos = work_buffer;

	if (db_name == NULL) {
		db_name = get_default_dbname(input_file->fn);
	}
	printf("Attempting to convert bam file %s into sqlite database %s\n", input_file->fn, db_name);

	rc = sqlite3_open(db_name, &db);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Error opening database: %s\n", sqlite3_errmsg(db));
		ret = 1;
		goto exit;
	}

	rc = sqlite3_exec(db, TABLE, NULL, NULL, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Error creating database: %s\n", err_msg);
		sqlite3_free(err_msg);
		ret = 1;
		goto exit;
	}

	/* YOLO SPEED */
	/* Allow OS to write data to disk asynchronously. Risks corruption if the machine goes down */
	sqlite3_exec(db, "PRAGMA synchronous = OFF", NULL, NULL, &err_msg);
	/* Don't need rollbacks for the initial insert. Data will corrupt if application crashes */
	sqlite3_exec(db, "PRAGMA journal_mode = OFF", NULL, NULL, &err_msg);

	header = sam_hdr_read(input_file);
	if (header == NULL) {
		fprintf(stderr, "Unable to read the header from %s\n", input_file->fn);
		ret = 1;
		goto exit;
	}

	sprintf(sql, "INSERT INTO seq VALUES (NULL, @QN, @BX, @BR);");
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Error preparing insert statement: %s\n", sqlite3_errmsg(db));
		ret = 1;
		goto exit;
	}

	bam_row = bam_init1();
	sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &err_msg);
	uint32_t n = 0;
	sqlite3_clear_bindings(stmt);
	sqlite3_reset(stmt);
	while ((r = sam_read1(input_file, header, bam_row)) >= 0) {
		buffer_pos = work_buffer;

		sqlite3_bind_text(stmt, 1, bam_get_qname(bam_row), -1, SQLITE_TRANSIENT); /* QNAME */
		sqlite3_bind_text(stmt, 2, bam_bx_str(bam_row, buffer_pos), -1, SQLITE_TRANSIENT); /* BX */
		sqlite3_bind_blob(stmt, 3, &bam_row, bam_row_size(bam_row), SQLITE_TRANSIENT); /* Full BAM row */

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "Error inserting row: %s\n", sqlite3_errmsg(db));
			ret = 1;
			goto exit;
		}

		sqlite3_clear_bindings(stmt);
		sqlite3_reset(stmt);

		if (++n % 100000 == 0) printf("%u rows inserted\n", n);
	}
	if (r < -1) {
		fprintf(stderr, "Attempting to process truncated file.\n");
		ret = 1;
		goto exit;
	}
	sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &err_msg);
	sqlite3_exec(db, "CREATE INDEX 'seq_BX_idx' ON 'seq' ('BX')", NULL, NULL, &err_msg);
	sqlite3_exec(db, "CREATE INDEX 'seq_QNAME_idx' ON 'seq' ('QNAME')", NULL, NULL, &err_msg);

exit:
	bam_destroy1(bam_row);
	free(work_buffer);
	sqlite3_close(db);
	return ret;
}
