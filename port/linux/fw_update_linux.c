// TODO - add implementation
//
// One idea is to do a "lightweight" OTA, where there is a loader process
// that dynamically loads the app as a .so.

#include "golioth_fw_update.h"
#include <unistd.h>  // readlink
#include <fcntl.h>   // open
#include <string.h>  // memcpy

#define TAG "fw_update_linux"

// If set to 1, any received FW blocks will be written to file DOWNLOADED_FILE_NAME
// This is mostly used for testing right now.
#define ENABLE_DOWNLOAD_TO_FILE 0
#define DOWNLOADED_FILE_NAME "downloaded.bin"

#define FW_UPDATE_RETURN_IF_NEGATIVE(expr) \
    do { \
        int retval = (expr); \
        if (retval < 0) { \
            return retval; \
        } \
    } while (0)

static FILE* _download_fp;
static FILE* _current_fp;
static uint8_t* _filebuf;
static bool _initialized;

bool fw_update_is_pending_verify(void) {
    return false;
}

void fw_update_rollback(void) {}

void fw_update_reboot(void) {}

void fw_update_cancel_rollback(void) {}

#if ENABLE_DOWNLOAD_TO_FILE
golioth_status_t fw_update_handle_block(
        const uint8_t* block,
        size_t block_size,
        size_t offset,
        size_t total_size) {
    if (!_download_fp) {
        _download_fp = fopen(DOWNLOADED_FILE_NAME, "w");
    }
    GLTH_LOGD(
            TAG,
            "block_size 0x%08lX, offset 0x%08lX, total_size 0x%08lX",
            block_size,
            offset,
            total_size);
    fwrite(block, block_size, 1, _download_fp);
    return GOLIOTH_OK;
}
#else
golioth_status_t fw_update_handle_block(
        const uint8_t* block,
        size_t block_size,
        size_t offset,
        size_t total_size) {
    return GOLIOTH_OK;
}
#endif

void fw_update_post_download(void) {
    if (_download_fp) {
        fclose(_download_fp);
        _download_fp = NULL;
    }
    if (_current_fp) {
        fclose(_current_fp);
        _current_fp = NULL;
    }
}

golioth_status_t fw_update_validate(void) {
    return GOLIOTH_OK;
}

golioth_status_t fw_update_change_boot_image(void) {
    return GOLIOTH_OK;
}

void fw_update_end(void) {
    if (_filebuf) {
        free(_filebuf);
        _filebuf = NULL;
    }
    _initialized = false;
}

// Opens filepath, allocates filebuf, and reads the entire contents into filebuf.
// The caller is responsible for freeing filebuf.
//
// On error, a negative value is returned
// On success, the number of bytes read is returned.
int read_file(const char* filepath, uint8_t** filebuf) {
    int fd = open(filepath, O_RDONLY, 0);
    FW_UPDATE_RETURN_IF_NEGATIVE(fd);

    int filesize = lseek(fd, 0, SEEK_END);
    FW_UPDATE_RETURN_IF_NEGATIVE(filesize);

    *filebuf = malloc(filesize + 1);
    if (!*filebuf) {
        GLTH_LOGE(TAG, "Failed to allocate");
        return -1;
    }

    FW_UPDATE_RETURN_IF_NEGATIVE(lseek(fd, 0, SEEK_SET));

    int bytes_read = read(fd, *filebuf, filesize);
    if (bytes_read != filesize) {
        GLTH_LOGE(TAG, "Failed to read entire file");
        return -1;
    }

    FW_UPDATE_RETURN_IF_NEGATIVE(close(fd));
    return bytes_read;
}

golioth_status_t fw_update_read_current_image_at_offset(
        uint8_t* buf,
        size_t bufsize,
        size_t offset) {
    static int filesize = 0;

    if (!_initialized) {
        char current_exe_path[256];

        // Get the path to the current running executable
        ssize_t nbytes = readlink("/proc/self/exe", current_exe_path, sizeof(current_exe_path));

        if (nbytes > 0) {
            GLTH_LOGI(TAG, "Current exe: %s", current_exe_path);
        } else {
            GLTH_LOGE(TAG, "Error getting path to current executable: %ld", nbytes);
            return GOLIOTH_ERR_FAIL;
        }

        int bytes_read = read_file(current_exe_path, &_filebuf);
        if (bytes_read < 0) {
            GLTH_LOGE(TAG, "error reading file");
            return GOLIOTH_ERR_FAIL;
        }
        filesize = bytes_read;

        _initialized = true;
    }

    GLTH_LOGD(TAG, "bufsize 0x%08lX, offset 0x%08lX", bufsize, offset);

    if (offset + bufsize > filesize) {
        GLTH_LOGE(
                TAG,
                "offset + bufsize is past the end of the file: %zd %zd %d",
                offset,
                bufsize,
                filesize);
        return GOLIOTH_ERR_FAIL;
    }

    return GOLIOTH_OK;
}
