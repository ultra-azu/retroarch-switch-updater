// Include the most common headers from the C standard library
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <curl/curl.h>
// Include the main libnx system header, for Switch development
#include <switch.h>
#include <minizip/unzip.h>
#define URL "http://buildbot.libretro.com/nightly/nintendo/switch/libnx/RetroArch_loader_update.zip"
#define ZIP_OUTPUT "/switch/RetroArch_loader_update.zip"
#define NRO_OUTPUT "/switch/retroarch_switch.nro"
#define BUFFER_SIZE 0x100000 // 1MiB
#define MAX_FILENAME 256

struct Buffer {
	FILE* file; // file to write to when buffer is full.
	uint8_t *buf; // buffer to read into
	size_t size; // max size of the buffer
	size_t offset; // offset (position in the buffer)
};

void clean_buffer(struct Buffer* buffer) {
	if (buffer->file) {
		fclose(buffer->file);
		buffer->file = NULL;
	}
	if (buffer->buf) {
		free(buffer->buf);
		buffer->buf = NULL;
	}
}

void flush_buffer(struct Buffer* buffer) {
	// if we have data
	if (buffer->offset > 0) {
		// todo: check for errors
		fwrite(buffer->buf, buffer->offset, 1, buffer->file);
	}
	// reset offset after writing
	buffer->offset = 0;
}

bool setup_buffer(struct Buffer* buffer, const char* path, size_t size) {
	memset(buffer, 0, sizeof(struct Buffer));
	if (!(buffer->file = fopen(path, "wb"))) {
		goto oof;
	}
	if (!(buffer->buf = malloc(size))) {
		goto oof;
	}
	buffer->size = size;

	return true;

oof:
	clean_buffer(buffer);
	return false;
}

size_t write_data(void *ptr, size_t size, size_t nmemb, void* _buffer) {
	struct Buffer *buffer = (struct Buffer*)_buffer;
	const size_t actual_size = size * nmemb;

	// this should never happen unless you fail to set the
	// buffer size or the buffer size is set to 32k.
	assert(buffer->size > actual_size);

	if ((buffer->offset + actual_size) > buffer->size) {
		flush_buffer(buffer);
	}

	// append the new data to the buffer->
	memcpy(buffer->buf + buffer->offset, ptr, actual_size);
	// move the position in the buffer->
	buffer->offset += actual_size;

	return actual_size;
}

int getfile() {
	//Blatantly taken from https://stackoverflow.com/questions/19404616/c-program-for-downloading-files-with-curl/19404752#19404752
	CURL *curl;
	CURLcode res;
	struct Buffer buffer = {0};
	const char *url = URL;

	if (!setup_buffer(&buffer, ZIP_OUTPUT, BUFFER_SIZE)) {
		puts("getfile(): Something went wrong when setting up buffer.");
		return 1;
	}

	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
		res = curl_easy_perform(curl);

		// flush rest of the data.
		flush_buffer(&buffer);
		clean_buffer(&buffer);

		curl_easy_cleanup(curl);
		if (res != 0) {
			puts("getfile(): Download somehow failed.");
			consoleUpdate(NULL);
			return 3;
		} else {
			return 0;
		}
	} else {
		puts("getfile(): Something failed when initting curl.");
		consoleUpdate(NULL);
		return 2;
	}
	curl_global_cleanup();
}

int unzipfile() {
	int ret = 0;
	unzFile zip = unzOpen(ZIP_OUTPUT);
	unz_global_info zipinfo;
	unzGetGlobalInfo(zip, &zipinfo);

	void *buf = malloc(BUFFER_SIZE);
	if (buf == NULL) {
		puts("unzipfile(): Failed to allocate buffer!");
		ret = 1;
		goto err;
	}

	for (size_t i = 0; i < zipinfo.number_entry; ++i) {
		char filename[MAX_FILENAME];
		unz_file_info fileinfo;

		unzOpenCurrentFile(zip);
		unzGetCurrentFileInfo(zip, &fileinfo, filename, sizeof(filename), NULL, 0, NULL, 0);
		if (strcmp(filename, "retroarch_switch.nro") == 0) {
			puts("Found .nro in zipfile.");
			FILE *outfile = fopen(NRO_OUTPUT, "wb");

			for (int j = unzReadCurrentFile(zip, buf, BUFFER_SIZE); j > 0; j = unzReadCurrentFile(zip, buf, BUFFER_SIZE))
				fwrite(buf, 1, j, outfile);
			fclose(outfile);
		} else {
			puts("unzipfile(): Couldn't find .nro in zipfile.");
			ret = 2;
		}
	}
err:
	unzCloseCurrentFile(zip);
	unzClose(zip);
	free(buf);
	return ret;
}

// Main program entrypoint
int main(void) {
	int ret;
	PadState pad;

	consoleInit(NULL);
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);
	socketInitializeDefault();

	printf("Do you want to update the RetroArch loader?\nPress + for returning to hbmenu, Y for updating.\n");
	consoleUpdate(NULL);

	while (appletMainLoop()) {
		padUpdate(&pad);
		u64 kDown = padGetButtonsDown(&pad);
		if (kDown & HidNpadButton_Plus)
			break;
		else if (kDown & HidNpadButton_Y) {
			ret = getfile();
			if (ret) {
				puts("Error: couldn't get file!");
				continue;
			} else
				puts("Downloaded zip!");
			ret = unzipfile();
			if (ret) {
				puts("Error: couldn't unzip file!");
				continue;
			} else {
				if (access(NRO_OUTPUT, F_OK) != -1) {
					puts("Unzipped and copied file! Retroarch has been updated.");
					remove(ZIP_OUTPUT);
				} else
					puts("Error: Even after unzipping, for whatever reason the nro doesn't exists.");
			}
		}
		consoleUpdate(NULL);
	}
	consoleExit(NULL);
	socketExit();
	return 0;
}


