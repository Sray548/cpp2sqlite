//
//  cpp2sqlite.cpp
//  cpp2sqlite
//
//  ©ywesee GmbH -- all rights reserved
//  License GPLv3.0 -- see License File
//  Created by Alex Bettarini on 15 Jan 2019
//

#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <exception>
#include <regex>

#include <sqlite3.h>
#include <libgen.h>     // for basename()

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>

#include "aips.hpp"
#include "refdata.hpp"
#include "swissmedic.hpp"
#include "bag.hpp"

#include "sqlDatabase.hpp"
#include "beautify.hpp"
#include "atc.hpp"
#include "epha.hpp"

namespace po = boost::program_options;
namespace pt = boost::property_tree;

void on_version()
{
    std::cout << "C++ " << __cplusplus << std::endl;
    std::cout << "SQLITE_VERSION: " << SQLITE_VERSION << std::endl;
    std::cout << "BOOST_VERSION: " << BOOST_LIB_VERSION << std::endl;
}

int countAipsPackagesInSwissmedic(AIPS::MedicineList &list)
{
    int count = 0;
    for (AIPS::Medicine m : list) {
        std::vector<std::string> regnrs;
        boost::algorithm::split(regnrs, m.regnrs, boost::is_any_of(", "), boost::token_compress_on);
        for (auto rn : regnrs) {
            count += SWISSMEDIC::countRowsWithRn(rn);
        }
    }
    return count;
}

int countBagGtinInSwissmedic(std::vector<std::string> &list)
{
    int count = 0;
    for (auto g : list) {
        if (SWISSMEDIC::findGtin(g))
            count++;
    }

    return count;
}

int countBagGtinInRefdata(std::vector<std::string> &list)
{
    int count = 0;
    for (auto g : list) {
        if (REFDATA::findGtin(g))
            count++;
    }
    
    return count;
}

void removeTagFromXml(std::string &xml, const std::string &tag)
{
    
}

// see RealExpertInfo.java:1065
void getHtmlFromXml(std::string &xml, std::string &html)
{
#if 1
    // cleanup: see also HtmlUtils.java:934
    std::regex r1(R"(<span[^>]*>)");
    xml = std::regex_replace(xml, r1, "");

    std::regex r2(R"(</span>)");
    xml = std::regex_replace(xml, r2, "");

    std::regex r3(R"(<sub[^>]*>)");
    xml = std::regex_replace(xml, r3, "");

    std::regex r4(R"(</sub>)");
    xml = std::regex_replace(xml, r4, "");

    std::regex r5(R"(<sup[^>]*>)");
    xml = std::regex_replace(xml, r5, "");
    
    std::regex r6(R"(</sup>)");
    xml = std::regex_replace(xml, r6, "");
    
    //std::clog << xml << std::endl;
#endif

    html = xml; // temporary
    return;

    pt::ptree tree;
    std::stringstream ss;
    ss << xml;
    read_xml(ss, tree);
    int seq=0;
    int sectionCount=0;
    int pCount=0;

    html = "<html>\n <head></head>\n <body>\n";

    try {
        BOOST_FOREACH(pt::ptree::value_type &v, tree.get_child("div")) {
            
            if (v.first == "p")
            {
                pt::ptree & attributes = v.second.get_child("<xmlattr>");
                
                std::clog
                << basename((char *)__FILE__) << ":" << __LINE__
                << ", seq " << ++seq
                << ", p count " << ++pCount
                << ", key <" << v.first.data() << ">"
                << ", val <" << v.second.data() << ">" // content of the tag
                << ", # children: " << v.second.size()
                << ", # attributes: " << attributes.size()
                << std::endl;
                
                int i=0;
                std::string section;
                std::string divClass;
                try {
                BOOST_FOREACH(pt::ptree::value_type &att, attributes) {
                    
                    //static int k=0;
                    //if (k++ < 14)
                        std::clog
                        << "\tattr " << ++i
                        << ", 1st: <" << att.first.data() << ">"
                        << ", 2nd: <" << att.second.data() << ">"
                        << std::endl;

                    if (att.first != "id")
                        continue;

                    section = att.second.data();
                    //std::cout << "\tLine: " << __LINE__ << ", section: <" << section.substr(0,7) << ">" << std::endl;

                    if (section.substr(0,7) == "section") {
                        //std::cout << "\tLine: " << __LINE__ << ", section: " << section << std::endl;
                        if (sectionCount++ == 0)
                            divClass = "MonTitle";
                        else
                            divClass = "paragraph";

                        html += "\n<div class=\"" + divClass + "\" id=\"" + section + "\">";
                        html += "\n</div>";
                    }
                } // BOOST attributes
                } catch (std::exception &e) {
                    std::cerr << basename((char *)__FILE__) << ":" << __LINE__ << ", Error" << e.what() << std::endl;
                }
            } // if p
            else if (v.first == "table") {
                std::clog << basename((char *)__FILE__) << ":" << __LINE__ << " table" << std::endl;
            } // if table
        } // BOOST div
    } catch (std::exception &e) {
        std::cerr << basename((char *)__FILE__) << ":" << __LINE__ << ", Error" << e.what() << std::endl;
    }

    html += "\n </body>\n</html>";
}

int main(int argc, char **argv)
{
    std::string appName = boost::filesystem::basename(argv[0]);

    std::string opt_downloadDirectory;
    std::string opt_language;
    bool flagXml = false;
    bool flagVerbose = false;
    //bool flagPinfo = false;
    std::string type("fi"); // Fachinfo
    std::string opt_aplha;
    std::string opt_regnr;
    std::string opt_owner;

    // See file Aips2Sqlite.java, function commandLineParse(), line 71, line 179
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "print this message")
        ("version,v", "print the version information and exit")
        ("verbose", "be extra verbose") // Show errors and logs
        ("nodown", "no download, parse only")
        ("lang", po::value<std::string>( &opt_language )->default_value("de"), "use given language (de/fr)")
        ("alpha", po::value<std::string>( &opt_aplha ), "only include titles which start with arg value")  // Med title
        ("regnr", po::value<std::string>( &opt_regnr ), "only include medications which start with arg value") // Med regnr
        ("owner", po::value<std::string>( &opt_owner ), "only include medications owned by arg value") // Med owner
        ("pseudo", "adds pseudo expert infos to db") // Pseudo fi
        ("inter", "adds drug interactions to db")
        ("pinfo", "generate patient info htmls") // Generate pi
        ("xml", "generate xml file")
        ("gln", "generate csv file with Swiss gln codes") // Shopping cart
        ("shop", "generate encrypted files for shopping cart")
        ("onlyshop", "skip generation of sqlite database")
        ("zurrose", "generate zur Rose full article database or stock/like files (fulldb/atcdb/quick)") // Zur Rose DB
        ("desitin", "generate encrypted files for Desitin")
        ("onlydesitin", "skip generation of sqlite database") // Only Desitin DB
        ("takeda", po::value<float>(), "generate sap/gln matching file")
        ("dailydrugcosts", "calculates the daily drug costs")
        ("smsequence", "generates swissmedic sequence csv")
        ("packageparse", "extract dosage information from package name")
        ("zip", "generate zipped big files (sqlite or xml)")
        ("reports", "generates various reports")
        ("indications", "generates indications section keywords report")
        ("plain", "does not update the package section")
        ("test", "starts in test mode")
        ("stats", po::value<float>(), "generates statistics for given user")
        ("inDir", po::value<std::string>( &opt_downloadDirectory )->required(), "download directory (without trailing /)")
        ;
    
    po::variables_map vm;

    try {
        po::store(po::parse_command_line(argc, argv, desc), vm); // populate vm
        
        if (vm.count("help")) {
            std::cout << desc << std::endl;
            return EXIT_SUCCESS;
        }
        
        if (vm.count("version")) {
            std::cout << appName << " " << __DATE__ << " " << __TIME__ << std::endl;
            on_version();
            return EXIT_SUCCESS;
        }

        po::notify(vm); // raise any errors encountered
    }
    catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (po::error& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (...) {
        std::cerr << "Exception of unknown type!" << std::endl;
        return EXIT_FAILURE;
    }

    if (vm.count("verbose")) {
        flagVerbose = true;
    }
    
    if (vm.count("xml")) {
        flagXml = true;
        std::cerr << basename((char *)__FILE__) << ":" << __LINE__ << " flagXml: " << flagXml << std::endl;
    }
    
    if (vm.count("pinfo")) {
        //flagPinfo = true;
        type = "pi";
        //std::cerr << basename((char *)__FILE__) << ":" << __LINE__ << " flagPinfo: " << flagPinfo << std::endl;
    }

#if 0
    // Read epha first, because aips needs to get missing ATC codes from it
    std::string jsonFilename = "/epha_products_" + opt_language + "_json.json";
    EPHA::parseJSON(opt_downloadDirectory + jsonFilename, flagVerbose);
#endif

    // Read swissmedic next, because aips might need to get missing ATC codes from it
    SWISSMEDIC::parseXLXS(opt_downloadDirectory + "/swissmedic_packages_xlsx.xlsx");

    ATC::parseTXT(opt_downloadDirectory + "/../input/atc_codes_multi_lingual.txt", opt_language, flagVerbose);

    AIPS::MedicineList &list = AIPS::parseXML(opt_downloadDirectory + "/aips_xml.xml", opt_language, type, flagVerbose);
    
    REFDATA::parseXML(opt_downloadDirectory + "/refdata_pharma_xml.xml", opt_language);
    
    std::cerr << "swissmedic has " << countAipsPackagesInSwissmedic(list) << " packages matching AIPS" << std::endl;
    
    BAG::parseXML(opt_downloadDirectory + "/bag_preparations_xml.xml", opt_language, flagVerbose);
    if (flagVerbose) {
        std::vector<std::string> bagList = BAG::getGtinList();
        std::cerr << "bag " << countBagGtinInSwissmedic(bagList) << " GTIN are also in swissmedic" << std::endl;
        std::cerr << "bag " << countBagGtinInRefdata(bagList) << " GTIN are also in refdata" << std::endl;
    }

    if (flagXml) {
        std::cerr << "Creating XML not yet implemented" << std::endl;
    }
    else {
        std::string dbFilename = "amiko_db_full_idx_" + opt_language + ".db";
        sqlite3 *db = AIPS::createDB(dbFilename);

        sqlite3_stmt *statement;
        AIPS::prepareStatement("amikodb", &statement,
                               "null, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?");

        std::clog << std::endl << "Populating " << dbFilename << std::endl;
        int statsRnFoundRefdataCount = 0;
        int statsRnNotFoundRefdataCount = 0;
        int statsRnFoundSwissmedicCount = 0;
        int statsRnNotFoundSwissmedicCount = 0;
        int statsRnFoundBagCount = 0;
        int statsRnNotFoundBagCount = 0;
        std::vector<std::string> statsRegnrsNotFound;

        for (AIPS::Medicine m : list) {
            // See DispoParse.java:164 addArticleDB()
            // See SqlDatabase.java:347 addExpertDB()
            AIPS::bindText("amikodb", statement, 1, m.title);
            AIPS::bindText("amikodb", statement, 2, m.auth);
            AIPS::bindText("amikodb", statement, 3, m.atc);
            AIPS::bindText("amikodb", statement, 4, m.subst);
            AIPS::bindText("amikodb", statement, 5, m.regnrs);
            
            // For each regnr in the vector add the name(s) from refdata
            std::vector<std::string> regnrs;
            boost::algorithm::split(regnrs, m.regnrs, boost::is_any_of(", "), boost::token_compress_on);
            //std::cerr << basename((char *)__FILE__) << ":" << __LINE__  << "regnrs size: " << regnrs.size() << std::endl;

            // tindex_str
            std::string tindex = BAG::getTindex(regnrs[0]);
            if (!tindex.empty())
                AIPS::bindText("amikodb", statement, 7, tindex);

            // application_str

            std::string application = SWISSMEDIC::getApplication(regnrs[0]);
            std::string appBag = BAG::getApplication(regnrs[0]);
            if (!appBag.empty())
                application += ";" + appBag;

            if (!application.empty())
                AIPS::bindText("amikodb", statement, 8, application);

#if 1
            // pack_info_str
            std::string packInfo;
            int rnCount=0;
            std::set<std::string> gtinUsed;
            for (auto rn : regnrs) {
                //std::cerr << basename((char *)__FILE__) << ":" << __LINE__  << " rn: " << rn << std::endl;
                std::string name = REFDATA::getNames(rn, gtinUsed);
                if (name.empty()) {
                    statsRnNotFoundRefdataCount++;
                }
                else {
                    if (rnCount>0)
                        packInfo += "\n";

                    packInfo += name;
                    rnCount++;
                    statsRnFoundRefdataCount++;
                }

                // Search in swissmedic
                name = SWISSMEDIC::getAdditionalNames(rn, gtinUsed);
                if (name.empty()) {
                    statsRnNotFoundSwissmedicCount++;
                }
                else {
                    if (rnCount>0)
                        packInfo += "\n";

                    packInfo += name;
                    rnCount++;
                    statsRnFoundSwissmedicCount++;
                }

                // Search in bag
                name = BAG::getAdditionalNames(rn, gtinUsed);
                if (name.empty()) {
                    statsRnNotFoundBagCount++;
                }
                else {
                    if (rnCount>0)
                        packInfo += "\n";
                    
                    packInfo += name;
                    rnCount++;
                    statsRnFoundBagCount++;
                }

                if (gtinUsed.empty())
                    statsRegnrsNotFound.push_back(rn);
            } // for

            packInfo = BEAUTY::sort(packInfo);

            if (!packInfo.empty())
                AIPS::bindText("amikodb", statement, 11, packInfo);
#endif

            // content

            {
#if 0
                std::clog << std::endl
                << basename((char *)__FILE__) << ":" << __LINE__
                << ", ================= " << tempCount
                << std::endl;
#endif
                std::string html;
                getHtmlFromXml(m.content, html);
                AIPS::bindText("amikodb", statement, 15, html);
                //AIPS::bindText("amikodb", statement, 15, m.content);
                
                //std::clog << std::endl << html << std::endl;
            }

            // packages
            // The line order must be the same as pack_info_str
            AIPS::bindText("amikodb", statement, 17, "|||CHF 0.00|CHF 0.00||||,,,|||255|0");
            
            // TODO: add all other columns

            AIPS::runStatement("amikodb", statement);
        }
        
        std::clog
        << "aips REGNRS (found/not found)" << std::endl
        << "\tin refdata: " << statsRnFoundRefdataCount << "/" << statsRnNotFoundRefdataCount << " (" << (statsRnFoundRefdataCount + statsRnNotFoundRefdataCount) << ")" << std::endl
        << "\tin swissmedic: " << statsRnFoundSwissmedicCount << "/" << statsRnNotFoundSwissmedicCount << " (" << (statsRnFoundSwissmedicCount + statsRnNotFoundSwissmedicCount) << ")" << std::endl
        << "\tin bag: " << statsRnFoundBagCount << "/" << statsRnNotFoundBagCount << " (" << (statsRnFoundBagCount + statsRnNotFoundBagCount) << ")" << std::endl
        << "\tnot found anywhere: " << statsRegnrsNotFound.size()
        << std::endl;

        REFDATA::printStats();
        SWISSMEDIC::printStats();
        BAG::printStats();

        if (statsRegnrsNotFound.size() > 0) {
            if (flagVerbose) {
                std::cout << "REGNRS not found anywhere:" << std::endl;
                for (auto s : statsRegnrsNotFound)
                    std::cout << s << ", ";
                
                std::cout << std::endl;
            }
            else {
                std::cerr << "Run with --verbose to see REGNRS not found" << std::endl;
            }
        }

        AIPS::destroyStatement(statement);

        int rc = sqlite3_close(db);
        if (rc != SQLITE_OK)
            std::cerr << basename((char *)__FILE__) << ":" << __LINE__ << ", rc" << rc << std::endl;
    }

    return EXIT_SUCCESS;
}
