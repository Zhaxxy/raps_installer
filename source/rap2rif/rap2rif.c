// (c) flatz

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "tools.h"

static const u8 rif_header[8] = {
	0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02,
};

static const u8 rif_footer[16] = {
	0x00, 0x00, 0x01, 0x2F, 0x41, 0x5C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const u8 rif_junk = 0x11;

static int load_from_file(const char *name, u8 *data, u32 length);
static int rap_to_klicensee(u8 *rap_key, u8 *klicensee);

int rap2rif(struct actdat *actdat, struct keylist *klist,char *new_content_id,char *rap_file, char *rif_file)
{
	FILE *fp = NULL;
	int i;

	u8 rap_key[16];
	u8 klicensee[16];
	u8 content_id[48];
	u8 padding[16];
	u8 rif_key[16];
	u8 enc_const[16];
	u8 dec_actdat[16];
	u8 signature[40];
	u32 actdat_key_index;






	if (load_from_file(rap_file, rap_key, sizeof(rap_key)) < 0) {
		goto fail;
	}

	memset(content_id, 0, sizeof(content_id));
	strcpy(content_id,new_content_id);

	memset(klicensee, 0, sizeof(klicensee));
	rap_to_klicensee(rap_key, klicensee);

	memset(padding, 0, sizeof(padding));
	memset(rif_key, 0, sizeof(rif_key));

	actdat_key_index = 0;
	wbe32(padding + sizeof(padding) - sizeof(actdat_key_index), actdat_key_index);

	aes128_enc(klist->idps->key, klist->npdrm_const->key, enc_const);
	aes128(enc_const, &actdat->keyTable[actdat_key_index * 16], dec_actdat);
	aes128_enc(klist->rif->key, padding, padding);
	aes128_enc(dec_actdat, klicensee, rif_key);

	memset(signature, rif_junk, sizeof(signature));

	fp = fopen(rif_file, "wb");
	if (fp == NULL) {
		goto fail;
	}
	fwrite(rif_header, sizeof(rif_header), 1, fp);
	fwrite(&actdat->unk1[0x08], 1, 8, fp);
	fwrite(content_id, sizeof(content_id), 1, fp);
	fwrite(padding, sizeof(padding), 1, fp);
	fwrite(rif_key, sizeof(rif_key), 1, fp);
	fwrite(rif_footer, sizeof(rif_footer), 1, fp);
	fwrite(signature, sizeof(signature), 1, fp);
	fclose(fp);

	return 0;

fail:
	if (fp != NULL) {
		fclose(fp);
	}

	if (actdat != NULL) {
		free(actdat);
	}

	if (klist != NULL) {
		if (klist->keys != NULL)
			free(klist->keys);
		free(klist);
	}

	return 1;
}

static int load_from_file(const char *path, u8 *data, u32 length)
{
	FILE *fp = NULL;
	u32 read;
	int ret = -1;
	fp = fopen(path, "rb");
	if (fp == NULL)
		goto fail;
	read = fread(data, length, 1, fp);
	if (read != 1)
		goto fail;
	ret = 0;

fail:
	if (fp != NULL)
		fclose(fp);
	return ret;
}

static int rap_to_klicensee(u8 *rap_key, u8 *klicensee)
{
	static u8 rap_initial_key[16] = {
		0x86, 0x9F, 0x77, 0x45, 0xC1, 0x3F, 0xD8, 0x90, 0xCC, 0xF2, 0x91, 0x88, 0xE3, 0xCC, 0x3E, 0xDF
	};
	static u8 pbox[16] = {
		0x0C, 0x03, 0x06, 0x04, 0x01, 0x0B, 0x0F, 0x08, 0x02, 0x07, 0x00, 0x05, 0x0A, 0x0E, 0x0D, 0x09
	};
	static u8 e1[16] = {
		0xA9, 0x3E, 0x1F, 0xD6, 0x7C, 0x55, 0xA3, 0x29, 0xB7, 0x5F, 0xDD, 0xA6, 0x2A, 0x95, 0xC7, 0xA5
	};
	static u8 e2[16] = {
		0x67, 0xD4, 0x5D, 0xA3, 0x29, 0x6D, 0x00, 0x6A, 0x4E, 0x7C, 0x53, 0x7B, 0xF5, 0x53, 0x8C, 0x74
	};

	u8 key[16];
	aes128(rap_initial_key, rap_key, key);

	for (int round_num = 0; round_num < 5; ++round_num) {
		// xor key byte array with e1 byte array (order does not matter)
		for (int i = 0; i < 16; ++i) {
			key[i] ^= e1[i];
		}
		// xor key byte array with its alternating self shifted by 1 in reversed byte order of pbox array
		// e.g. xor 2nd key byte in pbox order with 1st key byte in pbox order ([15]<[14],[14]<[13],...,[1]<[0])
		for (int i = 15; i >= 1; --i) {
			int p = pbox[i];
			int pp = pbox[i - 1];
			key[p] ^= key[pp];
		}
		// subtract e2 byte array from key byte array in byte order of pbox array
		u8 o = 0; // carry over
		for (int i = 0; i < 16; ++i) {
			int p = pbox[i];
			u8 ec2 = e2[p];
			//
			u8 kc = key[p] - o;
			key[p] = kc - ec2;
			// lowest result: 0x00 - 1 - 0xff = 0x00 with a carry over of 1
			// special case: carry over could cause another carry over only if intermediate result kc became 0xff (=0x00 - 1), then just keep carry over (no change)
			// otherwise carry over could occur only if intermediate result kc is less than subtraction value ec2
			if (o != 1 || kc != 0xFF) {
				o = kc < ec2 ? 1 : 0;
			}
		}
	}

	memcpy(klicensee, key, sizeof(key));
	return 0;
}