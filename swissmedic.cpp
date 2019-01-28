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
#include <set>
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
    std::vector<std::string> gtin;
    std::vector<std::string> category;
    
    int statsAugmentedRegnCount = 0;
    int statsAugmentedGtinCount = 0;
    int statsTotalGtinCount = 0;

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
        
        // Precalculate gtin
        std::string gtin12 = "7680" + rn5 + code3;
        char checksum = GTIN::getGtin13Checksum(gtin12);
        gtin.push_back(gtin12 + checksum);

        // Precalculate category
        std::string cat = aSingleRow[COLUMN_N];
        if ((cat == "A") && (aSingleRow[COLUMN_W] == "a"))
            cat += "+";
        
        category.push_back(cat);
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

            std::string paf = BAG::getPricesAndFlags(gtin[rowInt],
                                                     "ev.nn.i.H.", // TODO: localize
                                                     category[rowInt]);
            if (!paf.empty())
                names += paf;

            i++;
            statsTotalGtinCount++;
        }
    }
    
#if 0
    if (i>1)
        std::cout << basename((char *)__FILE__) << ":" << __LINE__ << " rn: " << rn << " FOUND " << i << " times" << std::endl;
#endif
    
    return names;
}
    
std::string getAdditionalNames(const std::string &rn,
                               const std::set<std::string> &gtinSet)
{
    std::string names;
    std::set<std::string>::iterator it;
    bool statsGtinAdded = false;
    
    for (int rowInt = 0; rowInt < theWholeSpreadSheet.size(); rowInt++) {
        std::string rn5 = regnrs[rowInt];
        if (rn5 != rn)
            continue;
        
        std::string g13 = gtin[rowInt];
        it = gtinSet.find(g13);
        if (it == gtinSet.end()) { // not found in refdata gtin set, we must add it
            statsGtinAdded = true;
            statsAugmentedGtinCount++;
            statsTotalGtinCount++;
            names += "\n";
            std::string name = theWholeSpreadSheet.at(rowInt).at(COLUMN_C);
#ifdef DEBUG_IDENTIFY_SWM_NAMES
            names += "swm+";
#endif
            names += name;
            
            std::string paf = BAG::getPricesAndFlags(g13,
                                                     "ev.nn.i.H.", // TODO: localize
                                                     category[rowInt]);
            if (!paf.empty())
                names += paf;
        }
    }
    
    if (statsGtinAdded)
        statsAugmentedRegnCount++;

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

std::string getCategoryFromGtin(const std::string &g)
{
    std::string cat;

    for (int rowInt = 0; rowInt < theWholeSpreadSheet.size(); rowInt++)
        if (gtin[rowInt] == g) {
            cat = category[rowInt];
            break;
        }

    return cat;
}

void printStats()
{
    std::cout
    << statsAugmentedRegnCount << " of the REGNRS found in refdata were augmented with a total of "
    << statsAugmentedGtinCount << " extra GTINs from swissmedic"
    << std::endl
    << "swissmedic total GTINs " << statsTotalGtinCount
    << std::endl;
}
    
}
