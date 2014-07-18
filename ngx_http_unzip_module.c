/**
 * @file   ngx_http_unzip_module.c
 * @author Bartek Jarocki <bartek@youzee.com>
 * @date   Fri Jun 15 10:52:12 2012
 *
 * @brief  This module extract files from unzip archive on the fly
 *
 * @section LICENSE
 *
 * Copyright (C) 2012 by Youzee Media Entertainment SL
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <string.h>
#include <stdio.h>
#include <zip.h>

static char *ngx_http_unzip(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_unzip_handler(ngx_http_request_t *r);

typedef struct {
    ngx_flag_t file_in_unzip;
    ngx_http_complex_value_t *file_in_unzip_archivefile;
    ngx_http_complex_value_t *file_in_unzip_extract;
} ngx_http_unzip_loc_conf_t;


/**
 * This module let you keep your files inside zip archive file
 * and serve (unzipping on the fly) them as requested.
 */
static ngx_command_t ngx_http_unzip_commands[] = {
    { 
      ngx_string("file_in_unzip"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_unzip,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL
    }, { 
      ngx_string("file_in_unzip_extract"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_unzip_loc_conf_t, file_in_unzip_extract), 
      NULL
    }, { 
      ngx_string("file_in_unzip_archivefile"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_unzip_loc_conf_t, file_in_unzip_archivefile), 
      NULL
    }, 
    ngx_null_command
};

/**
 * Create local configuration
 *
 * @param r
 *   Pointer to the request structure.
 * @return
 *   Pointer to the configuration structure.
 */
static void *
ngx_http_unzip_create_loc_conf(ngx_conf_t *cf) {

    ngx_http_unzip_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_unzip_loc_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }

    return conf;
}

/**
 * Merge configurations
 *
 * @param r
 *   Pointer to the request structure.
 * @return
 *   Status
 */
static char *
ngx_http_unzip_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {

    ngx_http_unzip_loc_conf_t *prev = parent;
    ngx_http_unzip_loc_conf_t *conf = child;

    if (conf->file_in_unzip_extract == NULL) {
        conf->file_in_unzip_extract = prev->file_in_unzip_extract;
    }

    if (conf->file_in_unzip_archivefile == NULL) {
        conf->file_in_unzip_archivefile = prev->file_in_unzip_archivefile;
    }

    return NGX_CONF_OK;
}

/* The module context. */
static ngx_http_module_t ngx_http_unzip_module_ctx = {
    NULL, /* preconfiguration */
    NULL, /* postconfiguration */

    NULL, /* create main configuration */
    NULL, /* init main configuration */

    NULL, /* create server configuration */
    NULL, /* merge server configuration */

    ngx_http_unzip_create_loc_conf, /* create location configuration */
    ngx_http_unzip_merge_loc_conf /* merge location configuration */
};


/* Module definition. */
ngx_module_t ngx_http_unzip_module = {
    NGX_MODULE_V1,
    &ngx_http_unzip_module_ctx, /* module context */
    ngx_http_unzip_commands, /* module directives */
    NGX_HTTP_MODULE, /* module type */
    NULL, /* init master */
    NULL, /* init module */
    NULL, /* init process */
    NULL, /* init thread */
    NULL, /* exit thread */
    NULL, /* exit process */
    NULL, /* exit master */
    NGX_MODULE_V1_PADDING
};

/**
 * Content handler.
 *
 * @param r
 *   Request structure pointer
 * @return
 *   Response status
 */
static ngx_int_t ngx_http_unzip_handler(ngx_http_request_t *r)
{
    ngx_buf_t   *b;
    ngx_chain_t out;
    ngx_str_t   unzip_filename;
    ngx_str_t   unzip_extract;
    struct      zip *zip_source;
    struct      zip_stat zip_st;
    struct      zip_file *file_in_zip;
    int         err = 0;
    char        *unzipfile_path;
    char        *unzipextract_path;
    unsigned char *zip_content;
    unsigned int  zip_read_bytes;

    ngx_http_unzip_loc_conf_t *unzip_config;
    unzip_config = ngx_http_get_module_loc_conf(r, ngx_http_unzip_module);

    /* let's try to get file_in_unzip_archivefile and file_in_unzip_extract from nginx configuration */
    if (ngx_http_complex_value(r, unzip_config->file_in_unzip_archivefile, &unzip_filename) != NGX_OK 
            || ngx_http_complex_value(r, unzip_config->file_in_unzip_extract, &unzip_extract) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to read unzip module configuration settings.");
        return NGX_ERROR;
    }

    /* we're supporting just GET and HEAD requests */
    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Only GET and HEAD requests are supported by the unzip module.");
        return NGX_HTTP_NOT_ALLOWED;
    }

    /* fill path variables with 0 as ngx_string_t doesn't terminate string with 0 */
    unzipfile_path = malloc(unzip_filename.len+1);
    if (unzipfile_path == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate buffer.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    unzipextract_path = malloc(unzip_extract.len+1);
    if (unzipextract_path == NULL) {
        free(unzipfile_path);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate buffer.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    memset(unzipfile_path, 0, unzip_filename.len+1);
    memset(unzipextract_path, 0, unzip_extract.len+1);

    /* get path variables terminated with 0 */
    strncpy(unzipfile_path, (char *)unzip_filename.data, unzip_filename.len);
    strncpy(unzipextract_path, (char *)unzip_extract.data, unzip_extract.len);

    /* try to open archive (zip) file */
    if (!(zip_source = zip_open(unzipfile_path, 0, &err))) {
        free(unzipfile_path);
        free(unzipextract_path);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "%s : no such archive file.", unzipfile_path);
        return NGX_HTTP_NOT_FOUND;
    }

    /* initialize structure */
    zip_stat_init(&zip_st);

    /* let's check what's the size of a file. return 404 if we can't stat file inside archive */
    if (0 != zip_stat(zip_source, unzipextract_path, 0, &zip_st)) {
        free(unzipfile_path);
        free(unzipextract_path);
        zip_close(zip_source);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "no file %s inside %s archive.", unzipextract_path, unzipfile_path);
        return NGX_HTTP_NOT_FOUND;
    }

    /* allocate buffer for the file content */
    if (!(zip_content = ngx_palloc(r->pool, zip_st.size))) {
        free(unzipfile_path);
        free(unzipextract_path);
        zip_close(zip_source);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate response buffer memory.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* 
    *  try to open a file that we want - if not return 500 as we know that the file is there (making zip_stat before) 
    *  so let's return 500.
    */
    if (!(file_in_zip = zip_fopen(zip_source, unzipextract_path, 0))) {
        free(unzipfile_path);
        free(unzipextract_path);
        zip_close(zip_source);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "failed to open %s from %s archive (corrupted?).",
                unzipextract_path, unzipfile_path);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* 
    *  let's get file content and check if we got all
    *  we're expecting to get zip_st.size bytes so return 500 if we get something else.
    */
    if (!(zip_read_bytes = zip_fread(file_in_zip, zip_content, zip_st.size)) || zip_read_bytes != zip_st.size) {
        free(unzipfile_path);
        free(unzipextract_path);
        zip_fclose(file_in_zip);
        zip_close(zip_source);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "couldn't get %d bytes of %s from %s archive (corrupted?).",
                zip_st.size, unzipextract_path, unzipfile_path);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* close both files */
    zip_fclose(file_in_zip);
    zip_close(zip_source);

    /* let's clean */
    free(unzipfile_path);
    free(unzipextract_path);

    /* set the content-type header. */
    if (ngx_http_set_content_type(r) != NGX_OK) {
        r->headers_out.content_type.len = sizeof("text/plain") - 1;
        r->headers_out.content_type.data = (u_char *) "text/plain";
    }

    /* allocate a new buffer for sending out the reply. */
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));

    if (b == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate response buffer.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* insertion in the buffer chain. */
    out.buf = b;
    out.next = NULL; /* just one buffer */

    b->pos = zip_content;
    b->last = zip_content + zip_read_bytes;
    b->memory = 1;
    b->last_buf = 1;

    /* sending the headers for the reply. */
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = zip_read_bytes;
    ngx_http_send_header(r);

    return ngx_http_output_filter(r, &out);
} /* ngx_http_unzip_handler */

/**
 * Configuration setup function that installs the content handler.
 *
 * @param cf
 *   Module configuration structure pointer.
 * @param cmd
 *   Module directives structure pointer.
 * @param conf
 *   Module configuration structure pointer.
 * @return string
 *   Status of the configuration setup.
 */
static char *ngx_http_unzip(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t *clcf; /* pointer to core location configuration */

    /* Install the unzip handler. */
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_unzip_handler;

    return NGX_CONF_OK;
} /* ngx_http_unzip */

