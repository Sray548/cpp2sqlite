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
//#include <clocale>
#include <algorithm>

#include <sqlite3.h>
#include <libgen.h>     // for basename()

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>
//#include <boost/program_options/errors.hpp>

#include "aips.hpp"
#include "refdata.hpp"
#include "swissmedic.hpp"
#include "bag.hpp"

#include "sqlDatabase.hpp"
#include "beautify.hpp"
#include "atc.hpp"
#include "epha.hpp"

#include "ean13/functii.h"

#define WITH_PROGRESS_BAR
#define USE_BOOST_FOR_REPLACEMENTS // 6m 16s, otherwise std::regex 13m 52s
//#define DEBUG_SHOW_RAW_XML_IN_DB_FILE

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

// Modify <colgroup>, see HtmlUtils.java:525
// Add all the values first into a `sum` variable,
// then each value is multiplied by 100 and divided by `sum`
//
// Special cases to be tested:
//  rn 65553 has an empty table before section 1
//  rn 56885 has only one col
void modifyColgroup(pt::ptree &colgroup)
{
    float sum = 0.0;
    std::vector<float> oldValue;

    BOOST_FOREACH(pt::ptree::value_type &col, colgroup) {
        std::string style = col.second.get<std::string>("<xmlattr>.style");
        
        float val = 0.0;
        // Test string: "width:1.77222in;"
        std::regex rgx(R"(\d*\.\d*)");  // tested at https://regex101.com
        std::smatch match;
        if (std::regex_search(style, match, rgx)) {
            val = std::stof(match[0]);
        }

        oldValue.push_back(val);
        sum += val;
    }

    int index = 0;
    BOOST_FOREACH(pt::ptree::value_type &col, colgroup) {
        float newValue = 100.0 * oldValue[index++] / sum;
        
        std::ostringstream s;
        s << std::fixed << std::setprecision(6) << newValue;
        std::string newStyle = "width:" + s.str() + "%25;background-color: #EEEEEE; padding-right: 5px; padding-left: 5px";

        col.second.put("<xmlattr>.style", newStyle);
    }
}

void removeTagFromXml(std::string &xml, const std::string &tag)
{
    
}

// see RealExpertInfo.java:1065
void getHtmlFromXml(std::string &xml,
                    std::string &html,
                    std::string regnrs,
                    std::string ownerCompany,
                    const std::set<std::string> &gtinUsed,
                    bool verbose)
{
#ifdef DEBUG_SHOW_RAW_XML_IN_DB_FILE
    html = xml;
    return;
#endif
    
    //std::clog << basename((char *)__FILE__) << ":" << __LINE__ << " " << regnrs << std::endl;

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
    
    std::regex r6a(R"(')");
    xml = std::regex_replace(xml, r6a, "&apos;"); // to prevent errors when inserting into sqlite table
    
    // This is time consuming and maybe unnecessary (TBC)
    // The Java version seems to be using Jsoup and EscapeMode.xhtml
    // Don't convert &lt; &gt; &apos;
#ifdef USE_BOOST_FOR_REPLACEMENTS
    boost::replace_all(xml, "&nbsp;",   " ");
    boost::replace_all(xml, "&micro;",  "µ");
    boost::replace_all(xml, "&auml;",   "ä");
    boost::replace_all(xml, "&ouml;",   "ö");
    boost::replace_all(xml, "&uuml;",   "ü");
    boost::replace_all(xml, "&Uuml;",   "Ü");
    boost::replace_all(xml, "&ge;",     "≥");
    boost::replace_all(xml, "&le;",     "≤");
    boost::replace_all(xml, "&agrave;", "à");
    boost::replace_all(xml, "&middot;", "–");
    boost::replace_all(xml, "&bdquo;",  "„");
    boost::replace_all(xml, "&ldquo;",  "“");
    boost::replace_all(xml, "&rsquo;",  "’");
    boost::replace_all(xml, "&beta;",   "β");
    boost::replace_all(xml, "&gamma;",  "γ");
    boost::replace_all(xml, "&frac12;", "½");
    boost::replace_all(xml, "&ndash;",  "–");
#else
    std::regex r7(R"(&nbsp;)");
    xml = std::regex_replace(xml, r7, " ");
    
    std::regex r8(R"(&micro;)");
    xml = std::regex_replace(xml, r8, "µ");

    std::regex r9(R"(&auml;)");
    xml = std::regex_replace(xml, r9, "ä");

    std::regex r10(R"(&ouml;)");
    xml = std::regex_replace(xml, r10, "ö");
    
    std::regex r11(R"(&uuml;)");
    xml = std::regex_replace(xml, r11, "ü");
    
    std::regex r11a(R"(&Uuml;)");
    xml = std::regex_replace(xml, r11a, "Ü");
    
    std::regex r12(R"(&ge;)");
    xml = std::regex_replace(xml, r12, "≥");

    std::regex r13(R"(&le;)");
    xml = std::regex_replace(xml, r13, "≤");
    
    std::regex r14(R"(&agrave;)");
    xml = std::regex_replace(xml, r14, "à");
    
    std::regex r15(R"(&middot;)");
    xml = std::regex_replace(xml, r15, "–");
    
    std::regex r16(R"(&bdquo;)");
    xml = std::regex_replace(xml, r16, "„");
    
    std::regex r17(R"(&ldquo;)");
    xml = std::regex_replace(xml, r17, "“");
    
    std::regex r17a(R"(&rsquo;)");
    xml = std::regex_replace(xml, r17a, "’");
    
    std::regex r18(R"(&beta;)");
    xml = std::regex_replace(xml, r18, "β");

    std::regex r19(R"(&gamma;)");
    xml = std::regex_replace(xml, r19, "γ");

    std::regex r20(R"(&frac12;)");
    xml = std::regex_replace(xml, r20, "½");

    std::regex r21(R"(&ndash;)");
    xml = std::regex_replace(xml, r21, "–");
#endif

    //std::clog << xml << std::endl;
#endif

    pt::ptree tree;
    std::stringstream ss;
    ss << xml;
    read_xml(ss, tree);
    int seq=0;
    int sectionNumber;
    int statsParCount=0;
    bool section1Done = false;
    bool section18Done = false;

    html = "<html>\n";
    html += " <head></head>\n";
    html += " <body>\n";
    html += "  <div id=\"monographie\" name=\"" + regnrs + "\">\n\n";

    try {
        BOOST_FOREACH(pt::ptree::value_type &v, tree.get_child("div")) {
  
            std::string tagContent = v.second.data(); // unfortunately it changes &lt; into <
//            if (tagContent.empty()) {
//                std::clog << basename((char *)__FILE__) << ":" << __LINE__ << std::endl;
//                continue;
//            }

            // Undo then undesired replacements done by boost xml_parser
#ifdef USE_BOOST_FOR_REPLACEMENTS
            boost::replace_all(tagContent, "<", "&lt;");
            boost::replace_all(tagContent, ">", "&gt;");
            boost::replace_all(tagContent, "'", "&apos;");
#else
            std::regex r1("<");
            tagContent = std::regex_replace(tagContent, r1, "&lt;");
            
            std::regex r2(">");
            tagContent = std::regex_replace(tagContent, r2, "&gt;");
            
            std::regex r3("'");
            tagContent = std::regex_replace(tagContent, r3, "&apos;");
#endif

#if 0
            std::clog
            << basename((char *)__FILE__) << ":" << __LINE__
            << ", seq " << ++seq
            << ", # children: " << v.second.size()
            << ", key <" << v.first.data() << ">"
            << ", val <" << tagContent << ">"
            << std::endl;
#endif
            
            if (v.first == "p")
            {
                ++statsParCount;

                bool isSection = true;
                std::string section;
                try {
                    section = v.second.get<std::string>("<xmlattr>.id");
                    sectionNumber = std::stoi(section.substr(7));
                    
                    std::string divClass;
                    if (sectionNumber == 1) {
                        divClass = "MonTitle";
                    }
                    else {
                        divClass = "paragraph";
                        tagContent = " <div class=\"absTitle\">\n " + tagContent + "\n </div>\n";
                    }
                    
                    if (sectionNumber > 1)
                        html += "   </div>\n"; // terminate previous section before starting a new one

                    html += "   <div class=\"" + divClass + "\" id=\"" + section + "\">\n";
                    html += tagContent + "\n";
                    //html += "   </div>\n";  // don't terminate the div as yet
#if 0
                    std::clog
                    << basename((char *)__FILE__) << ":" << __LINE__
                    << ", p count " << statsParCount
                    << ", attr id: <" << section << ">"
                    << ", nr: <" << sectionNumber << ">"
                    << std::endl;
#endif
                    if (sectionNumber == 1) {
                        // ownerCompany is a separate div between section 1 and section 2
                        html += "   </div>\n";  // terminate previous section
                        html += "   <div class=\"ownerCompany\">\n";
                        html += "    <div style=\"text-align: right;\">\n   ";
                        html += ownerCompany + "\n";
                        html += "    </div>\n";
                        //html += "   </div>\n";    // don't terminate the div as yet
                        section1Done = true;
                    }

                    // see RealExpertInfo.java:1562
                    // see BarCode.java:77
                    if (sectionNumber == 18) {
                        for (auto gtin : gtinUsed) {
                            std::string svg = EAN13::createSvg("", gtin);
                            // TODO: onmouseup="addShoppingCart(this)"
                            html += "<p class=\"barcode\">" + svg + "</p>\n";
                        }

                        section18Done = true;
                    }

                    continue;
                }
                catch (std::exception &e) {
                    isSection = false;
                    //std::cerr << basename((char *)__FILE__) << ":" << __LINE__ << ", Error " << e.what() << std::endl;
                }  // try section

                // Skip before section 1
                if (!section1Done)
                    continue;
                
                // Skip all the remaining before section 2
                if ((sectionNumber < 2) && (section1Done))
                    continue;

                if (sectionNumber == 13) {
                    bool imgFound = false;
                    BOOST_FOREACH(pt::ptree::value_type &v2, v.second) {
                        if (v2.first == "img") {
                            imgFound = true;

                            std::string img = "<img";

                            std::string src = v2.second.get<std::string>("<xmlattr>.src");
                            img += " Src=\"" + src + "\"";

                            std::string style = v2.second.get<std::string>("<xmlattr>.style");
                            if (!style.empty())
                                img += " Style=\"" + style + "\"";
                            
                            std::string alt;
                            try {
                                alt = v2.second.get<std::string>("<xmlattr>.alt");
                                if (!alt.empty())
                                    img += " Alt=\"" + alt + "\"";
                            }
                            catch (std::exception &e) {
                                std::cerr
                                << basename((char *)__FILE__) << ":" << __LINE__
                                << ", regnrs: " << regnrs
                                << ", <img> Warning " << e.what() << std::endl;
                            }

                            img += " />";

                            html += "  <ppp class=\"spacing1\">" + img + "</ppp>\n";
                        }
                    } // BOOST
                    
                    if (imgFound)
                        continue;  // already added this <p> to html
                } // section 13

                // Skip all the remaining in section 18
                if ((sectionNumber == 18) && (section18Done))
                    continue;

                // See HtmlUtils.java:472
                bool needItalicSpan = true;
                boost::algorithm::trim(tagContent); // sometimes it ends with ". "
                
                if (tagContent.empty()) // for example in rn 51704
                    continue; // TBC

                if (boost::ends_with(tagContent, ".") ||
                    boost::ends_with(tagContent, ",") ||
                    boost::ends_with(tagContent, ":") ||
                    boost::contains(tagContent, "ATC-Code") ||
                    boost::contains(tagContent, "Code ATC"))
                {
                    needItalicSpan = false;
                }
                
                // See HtmlUtils.java:602
#if 0
                std::regex r("^[–|·|-|•]");
                tagContent = std::regex_replace(tagContent, r, "– "); // FIXME: it becomes "– \200\223 "
#else
                if (boost::starts_with(tagContent, "–")) {      // en dash
                    boost::replace_first(tagContent, "–", "– ");
                    needItalicSpan = false;
                }
                else if (boost::starts_with(tagContent, "·")) {
                    boost::replace_first(tagContent, "·", "– ");
                    needItalicSpan = false;
                }
                else if (boost::starts_with(tagContent, "-")) { // hyphen
                    boost::replace_first(tagContent, "-", "– ");
                    needItalicSpan = false;
                }
                else if (boost::starts_with(tagContent, "•")) {
                    boost::replace_first(tagContent, "•", "– ");
                    needItalicSpan = false;
                }
//                else if (boost::starts_with(tagContent, "*")) {  // TBC see table footnote for rn 51704
//                    needItalicSpan = false;
//                }
#endif

                if (needItalicSpan)
                    tagContent = "<span style=\"font-style:italic;\">" + tagContent + "</span>";

                html += "  <p class=\"spacing1\">" + tagContent + "</p>\n";

            } // if p
            else if (v.first == "table") {
#if 0
                std::clog
                << basename((char *)__FILE__) << ":" << __LINE__
                << ", table" << v.second.data()
                << std::endl;
#endif

                // Normalize column widths to a percentage value
                pt::ptree &colgroup = v.second.get_child("colgroup");
                modifyColgroup(colgroup);

                // Purpose: add the table to the html "as is"
                // Method: create a new property tree, string based, not file based
                //         and add the whole table object as the only child
                pt::ptree tree;
                std::stringstream ss;
                tree.add_child("table", v.second);
                pt::write_xml(ss, tree);
                
                // Clean up the "serialized" string
                std::string table = ss.str();
                boost::replace_all(table, "<?xml version=\"1.0\" encoding=\"utf-8\"?>", "");

#if 0
                std::clog
                << basename((char *)__FILE__) << ":" << __LINE__
                << ", table" << table
                << std::endl;
#endif

                html += table + "\n";
            } // if table
        } // BOOST div
    } catch (std::exception &e) {
        std::cerr << basename((char *)__FILE__) << ":" << __LINE__ << ", Error " << e.what() << std::endl;
    }

    // TODO: section21 Auto-generated by appName + timestamp

    html += "\n  </div>";
    html += "\n </body>";
    html += "\n</html>";
}

int main(int argc, char **argv)
{
    //std::setlocale(LC_ALL, "en_US.utf8");

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

#ifdef WITH_PROGRESS_BAR
        int ii=1;
        int n=list.size();
#endif
        for (AIPS::Medicine m : list) {
            
#ifdef WITH_PROGRESS_BAR
            // Show progress
            if ((ii++ % 60) == 0)
                std::cerr << "\r" << 100*ii/n << " % ";
#endif

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
            if (tindex.empty())
                AIPS::bindText("amikodb", statement, 7, "");
            else
                AIPS::bindText("amikodb", statement, 7, tindex);

            // application_str

            std::string application = SWISSMEDIC::getApplication(regnrs[0]);
            std::string appBag = BAG::getApplication(regnrs[0]);
            if (!appBag.empty())
                application += ";" + appBag;

            if (application.empty())
                AIPS::bindText("amikodb", statement, 8, "");
            else
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

            if (packInfo.empty())
                AIPS::bindText("amikodb", statement, 11, "");
            else
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
                getHtmlFromXml(m.content, html, m.regnrs, m.auth, gtinUsed, flagVerbose);
                AIPS::bindText("amikodb", statement, 15, html);
            }

            // packages
            // The line order must be the same as pack_info_str
            AIPS::bindText("amikodb", statement, 17, "|||CHF 0.00|CHF 0.00||||,,,|||255|0");
            
            // TODO: add all other columns

            AIPS::runStatement("amikodb", statement);
        } // for
        
#ifdef WITH_PROGRESS_BAR
        std::cerr << "\r100 %" << std::endl;
#endif
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
