## What is this?
A nginx module enabling fetching of files that are stored in zipped archives.

## Nginx configuration example

* file_in_unzip_archivefile - points to the zipped file
* file_in_unzip_extract - file to be extracted from the zipped file
* file_in_unzip - flag activating the module

<pre>
  location ~ "^([a-zA-Z0-9-/]+)/([a-zA-Z0-9-.]+)(Seg1-Frag\d+)$" {
    file_in_unzip_archivefile $document_root$1/$2;
    file_in_unzip_extract $2$3;
    file_in_unzip;
  }
</pre>
