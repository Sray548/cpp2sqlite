//
//  swissmedic.cpp
//  cpp2sqlite
//
//  ©ywesee GmbH -- all rights reserved
//  License GPLv3.0 -- see License File
//  Created by Alex Bettarini on 22 Jan 2019
//

#include <iostream>
#include <libgen.h>     // for basename()
#include <xlnt/xlnt.hpp>

#include "swissmedic.hpp"
#include "gtin.hpp"
#include "bag.hpp"

#define COLUMN_A        0   // GTIN (5 digits)
#define COLUMN_C        2   // name
#define COLUMN_K       10   // packaging code (3 digits)
#define COLUMN_N       13   // category (A..E)
#define COLUMN_S       18   // application field
#define COLUMN_W       22   // preparation contains narcotics

#define FIRST_DATA_ROW_INDEX    5

namespace SWISSMEDIC
{
    std::vector< std::vector<std::string> > theWholeSpreadSheet;
    std::vector<std::string> regnrs;        // padded to 5 characters (digits)
    std::vector<std::string> packingCode;   // padded to 3 characters (digits)

void parseXLXS(const std::string &filename)
{
    xlnt::workbook wb;
    wb.load(filename);
    auto ws = wb.active_sheet();

    std::clog << std::endl << "Reading swissmedic XLSX" << std::endl;

    int skipHeaderCount = 0;
    for (auto row : ws.rows(false)) {
        if (++skipHeaderCount <= FIRST_DATA_ROW_INDEX)
            continue;

        std::vector<std::string> aSingleRow;
        for (auto cell : row) {
            //std::clog << cell.to_string() << std::endl;
            aSingleRow.push_back(cell.to_string());
        }

        theWholeSpreadSheet.push_back(aSingleRow);

        // Precalculate padded regnr
        std::string rn5 = GTIN::padToLength(5, aSingleRow[COLUMN_A]);
        regnrs.push_back(rn5);

        // Precalculate padded packing code
        std::string code3 = GTIN::padToLength(3, aSingleRow[COLUMN_K]);
        packingCode.push_back(code3);
    }

    std::clog << "swissmedic rows: " << theWholeSpreadSheet.size() << std::endl;
}

// Note that multiple rows can have the same value in column A
// Each row corresponds to a different package (=gtin_13)
std::string getNames(const std::string &rn)
{
    std::string names;
    int i=0;
    for (int rowInt = 0; rowInt < theWholeSpreadSheet.size(); rowInt++) {
        std::string rn5 = regnrs[rowInt];

        // TODO: to speed up do a numerical comparison so that we can return when gtin5>rn
        // assuming that column A is sorted
        if (rn5 == rn) {
            if (i>0)
                names += "\n";

            std::string name = theWholeSpreadSheet.at(rowInt).at(COLUMN_C);
#ifdef DEBUG_IDENTIFY_SWM_NAMES
            names += "swm-";
#endif
            names += name;

            std::string gtin = getGtinFromRow(rowInt);
            std::string cat = getCategoryFromRow(rowInt);
            std::string paf = BAG::getPricesAndFlags(gtin, cat);

            if (!paf.empty()) {
                names += paf;
#if 0
                static int k=0;
                if (k++ < 13)
                    std::clog
                    << basename((char *)__FILE__) << ":" << __LINE__
                    << ", k:" << k
                    << ", name: " << name
                    << ", paf: " << paf
                    << std::endl;
#endif
            }

            i++;
        }
    }
    
#if 0
    if (i>1)
        std::cout << basename((char *)__FILE__) << ":" << __LINE__ << " rn: " << rn << " FOUND " << i << " times" << std::endl;
#endif
    
    return names;
}

int countRowsWithRn(const std::string &rn)
{
    int count = 0;
    for (int rowInt = 0; rowInt < theWholeSpreadSheet.size(); rowInt++) {
        std::string gtin_5 = regnrs[rowInt];
        // TODO: to speed up do a numerical comparison so that we can return when gtin5>rn
        // assuming that column A is sorted
        if (gtin_5 == rn)
            count++;
    }

    return count;
}

std::string getGtinFromRow(const int rowInt)
{
    std::string rn5 = regnrs[rowInt];
    std::string code3 = packingCode[rowInt];
    std::string gtin12 = "7680" + rn5 + code3;
    char checksum = GTIN::getGtin13Checksum(gtin12);
    return gtin12 + checksum;
}

bool findGtin(const std::string &gtin)
{
    for (int rowInt = 0; rowInt < theWholeSpreadSheet.size(); rowInt++) {
        std::string rn5 = regnrs[rowInt];
        std::string code3 = packingCode[rowInt];
        std::string gtin12 = "7680" + rn5 + code3;

#if 0
        // We could also recalculate and verify the checksum
        // but such verification has already been done when parsing the files
        char checksum = GTIN::getGtin13Checksum(gtin12);
        
        if (checksum != gtin[12]) {
            std::cerr
            << basename((char *)__FILE__) << ":" << __LINE__
            << ", GTIN error, expected:" << checksum
            << ", received" << gtin[12]
            << std::endl;
        }
#endif

        // The comparison is only the first 12 digits, without checksum
        if (gtin12 == gtin.substr(0,12)) // pos, len
            return true;
    }

    return false;
}

std::string getApplication(const std::string &rn)
{
    std::string app;
    for (int rowInt = 0; rowInt < theWholeSpreadSheet.size(); rowInt++) {
        if (rn == regnrs[rowInt]) {
            app = theWholeSpreadSheet.at(rowInt).at(COLUMN_S) + " (Swissmedic)";
            break;
        }
    }

    return app;
}

std::string getCategoryFromRow(const int rowInt)
{
    std::string category;
    std::string modifier;
    
    category = theWholeSpreadSheet.at(rowInt).at(COLUMN_N);
    modifier = theWholeSpreadSheet.at(rowInt).at(COLUMN_W);
    if ((category == "A") && (modifier == "a"))
        category += "+";
    
    return category;
}
    
std::string getCategoryFromGtin(const std::string &gtin)
{
    std::string category;
    //std::string modifier;

    for (int rowInt = 0; rowInt < theWholeSpreadSheet.size(); rowInt++) {
        std::string gtin13 = getGtinFromRow(rowInt);
        if (gtin13 == gtin) {
            category = getCategoryFromRow(rowInt);
            break;
        }
    }

    return category;
}

}
