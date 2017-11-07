#include <stdlib.h>  // abs
#include <string>  // memcpy
#include "datatable.h"
#include "utils/assert.h"
#include "utils.h"



/**
 * Load DataTable stored in NFF format on disk.
 *
 * colspec
 *     A DataTable object containing information about the columns of the
 *     datatable stored on disk. This object should contain 3 columns, with
 *     file names, stypes, and meta-information of each column in the stored
 *     datatable.
 *
 * nrows
 *     Number of rows in the stored datatable.
 */
DataTable* DataTable::load(DataTable* colspec, int64_t nrows, const std::string& path)
{
    int64_t ncols = colspec->nrows;
    Column** columns = NULL;
    dtmalloc(columns, Column*, ncols + 1);
    columns[ncols] = NULL;

    if (colspec->ncols != 3) {
        throw ValueError() << "colspec table should have had 3 columns, "
                           << "but " << colspec->ncols << " were passed";
    }
    SType stypef = colspec->columns[0]->stype();
    SType stypes = colspec->columns[1]->stype();
    SType stypem = colspec->columns[2]->stype();
    if (stypef != ST_STRING_I4_VCHAR ||
        stypes != ST_STRING_I4_VCHAR ||
        stypem != ST_STRING_I4_VCHAR) {
        throw ValueError() << "String columns are expected in colspec table, "
                           << "instead got " << stypef << ", "
                           << stypes << ", and " << stypem;
    }

    StringColumn<int32_t>* colf =
        static_cast<StringColumn<int32_t>*>(colspec->columns[0]);
    StringColumn<int32_t>* cols =
        static_cast<StringColumn<int32_t>*>(colspec->columns[1]);
    StringColumn<int32_t>* colm =
        static_cast<StringColumn<int32_t>*>(colspec->columns[2]);

    int32_t* offf = colf->offsets();
    int32_t* offs = cols->offsets();
    int32_t* offm = colm->offsets();


    /*static char filename[1001];
    static char metastr[101];
    size_t len = path.length();
    if (len > 900) {
        throw ValueError() << "The path is too long: " << path;
    }*/
    for (int64_t i = 0; i < ncols; ++i)
    {
        // Extract filename
        size_t fsta = static_cast<size_t>(abs(offf[i - 1]));
        size_t fend = static_cast<size_t>(abs(offf[i]));
        size_t flen = static_cast<size_t>(fend - fsta);
        /*if (flen > 100) {
            throw ValueError() << "Filename is too long: " << flen;
        }
        memcpy(ffilename, colf->data_at(static_cast<size_t>(fsta)), (size_t) flen);
        ffilename[flen] = '\0';*/
        std::string filename(path);
        if (!(filename.empty() || filename.back() == '/'))
          filename += "/";
        filename.append(colf->strdata() + fsta, flen);

        // Extract stype
        size_t ssta = static_cast<size_t>(abs(offs[i - 1]));
        size_t send = static_cast<size_t>(abs(offs[i]));
        size_t slen = static_cast<size_t>(send - ssta);
        if (slen != 3) {
            throw ValueError() << "Incorrect stype's length: " << slen;
        }
        std::string stype_str(cols->strdata() + ssta, slen);
        SType stype = stype_from_string(stype_str);
        if (stype == ST_VOID) {
            throw ValueError() << "Unrecognized stype: " << stype_str;
        }

        // Extract meta info (as a string)
        size_t msta = static_cast<size_t>(abs(offm[i - 1]));
        size_t mend = static_cast<size_t>(abs(offm[i]));
        size_t mlen = static_cast<size_t>(mend - msta);
        /*if (mlen > 100) {
            throw ValueError() << "Meta string is too long: " << mlen;
        }*/
        std::string metastr(colm->strdata() + msta, mlen);

        // Load the column
        columns[i] = Column::open_mmap_column(stype, nrows, filename, metastr);
    }

    return new DataTable(columns);
}