//
//  peddose.cpp
//  cpp2sqlite
//
//  ©ywesee GmbH -- all rights reserved
//  License GPLv3.0 -- see License File
//  Created by Alex Bettarini on 15 Feb 2019
//

#include <iostream>
#include <set>
#include <map>
#include <libgen.h>     // for basename()
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>

#include "peddose.hpp"

#define WITH_SEPARATE_TABLE_HEADER

namespace pt = boost::property_tree;

namespace PED
{
    // Parse-phase stats
    unsigned int statsCasesCount = 0;
    unsigned int statsIndicationsCount = 0;
    unsigned int statsCodesCount = 0;
    unsigned int statsDosagesCount = 0;

    unsigned int statsCode_ALTERRELATION = 0;
    unsigned int statsCode_FG = 0;
    unsigned int statsCode_GEWICHT = 0;
    unsigned int statsCodeAtc = 0;
    unsigned int statsCodeDOSISTYP = 0;
    unsigned int statsCodeDOSISUNIT = 0;
    unsigned int statsCodeEVIDENZ = 0;
    unsigned int statsCodeRoa = 0;
    unsigned int statsCodeZEIT = 0;
    
    // Usage stats
    unsigned int statsCasesForAtcFoundCount = 0;
    unsigned int statsCasesForAtcNotFoundCount = 0;

    std::vector<_case> caseVec;
    std::set<std::string> caseCaseIDSet; // TODO: obsolete
    std::set<std::string> caseAtcCodeSet;// TODO: obsolete
    std::set<std::string> caseRoaCodeSet;// TODO: obsolete
    
    std::map<std::string, _indication> indicationMap; // key is IndicationKey

    std::map<std::string, _code> codeAlterMap; // key is CodeValue
    std::map<std::string, _code> codeAtcMap; // key is CodeValue
    std::map<std::string, _code> codeDosisUnitMap; // key is CodeValue
    std::vector<_code> codeRoaVec;
    std::set<std::string> codeRoaCodeSet;

    std::vector<_dosage> dosageVec;
    std::set<std::string> dosageCaseIDSet;// TODO: obsolete
    
    std::map<std::string, std::string> abbreviationsLookup = {
        {"Dosis", "dose"},
        {"Gramm", "g"}, // TODO: see <Code><CodeType>DOSISUNIT</CodeType><CodeValue>Gramm</CodeValue>
                        // see also  <Code><CodeType>_GEWICHT</CodeType><CodeValue>Gramm</CodeValue>
        {"Kilogramm", "kg"},
        {"Milligramm", "mg"},
        {"Tag", "day"}
    };
    
static std::string getAbbreviation(const std::string s)
{
    // TODO: use codeDosisUnitMap instead
    std::map<std::string, std::string>::iterator it;
    it = abbreviationsLookup.find(s);
    if (it != abbreviationsLookup.end())
        return it->second;
    
    return s;
}

void parseXML(const std::string &filename,
              const std::string &language)
{
    pt::ptree tree;
    
    try {
        std::clog << std::endl << "Reading Ped XML" << std::endl;
        pt::read_xml(filename, tree);
    }
    catch (std::exception &e) {
        std::cerr << "Line: " << __LINE__ << " Error " << e.what() << std::endl;
    }
    
    std::clog << "Analyzing Ped " << language << std::endl;
    int i=0;

    try {
        BOOST_FOREACH(pt::ptree::value_type &v, tree.get_child("SwissPedDosePublication")) {
            if (v.first == "Cases") {
                statsCasesCount = v.second.size();
            }
            else if (v.first == "Indications") {
                statsIndicationsCount = v.second.size();
            }
            else if (v.first == "Codes") {
                statsCodesCount = v.second.size();
            }
            else if (v.first == "Dosages") {
                statsDosagesCount = v.second.size();
            }
        }  // FOREACH
        
        i = 0;
        BOOST_FOREACH(pt::ptree::value_type &v, tree.get_child("SwissPedDosePublication.Cases")) {
            if (v.first == "Case") {
                /*
                 <ATCCode>J01CA04</ATCCode>
                 <IndicationKey>339</IndicationKey>
                 */
#if 0

                std::clog
                << basename((char *)__FILE__) << ":" << __LINE__
                << ", i: " << i++
                << ", # children: " << v.second.size()
                //<< ", key <" << v.first.data() << ">"       // Case
                << ", CaseTitle <" << v.second.get("CaseTitle", "") << ">"
                << std::endl;
#endif
                caseCaseIDSet.insert(v.second.get("CaseID", ""));
                caseAtcCodeSet.insert(v.second.get("ATCCode", ""));
                caseRoaCodeSet.insert(v.second.get("ROACode", ""));
                
                _case ca;
                ca.caseId = v.second.get("CaseID", "");
                ca.atcCode = v.second.get("ATCCode", "");
                ca.indicationKey = v.second.get("IndicationKey", "");
                ca.RoaCode = v.second.get("ROACode", "");
                caseVec.push_back(ca);
            }
        } // FOREACH Cases

        i = 0;
        BOOST_FOREACH(pt::ptree::value_type &v, tree.get_child("SwissPedDosePublication.Indications")) {
            if (v.first == "Indication") {
                
                /* D German, F French, E English
                 
                 <IndicationKey>641</IndicationKey>
                 <IndikationNameE>Treatment of mild to moderately severe, acute pain</IndikationNameE>
                 */
#if 0

                std::clog
                << basename((char *)__FILE__) << ":" << __LINE__
                << ", i: " << i++
                << ", # children: " << v.second.size()
                << ", IndicationNameD <" << v.second.get("IndicationNameD", "") << ">"
                << std::endl;
#endif
                _indication in;
                if (language == "de")
                    in.name = v.second.get("IndicationNameD", "");
                else if (language == "fr")
                    in.name = v.second.get("IndicationNameF", "");
                else
                    in.name = v.second.get("IndikationNameE", ""); // English has a K

                in.recStatus = v.second.get("RecStatus", "");
                indicationMap.insert(std::make_pair(v.second.get("IndicationKey", ""), in));
            }
        } // FOREACH Indications

        i = 0;
        BOOST_FOREACH(pt::ptree::value_type &v, tree.get_child("SwissPedDosePublication.Codes")) {
            if (v.first == "Code") {
                std::string codeType = v.second.get("CodeType", "");
                
                _code co;
                co.value = v.second.get("CodeValue", "");
                if (language == "de")
                    co.description = v.second.get("DescriptionD", "");
                else if (language == "fr")
                    co.description = v.second.get("DecsriptionF", "");  // Note: spelling mistake
                else //if (language == "en")
                    co.description = v.second.get("DecriptionE", "");
                co.recStatus = v.second.get("RecStatus", "");

                if (codeType == "_ALTERRELATION") {
                    statsCode_ALTERRELATION++;
                }
                else if (codeType == "_FG") {
                    statsCode_FG++;
                }
                else if (codeType == "ATC") {
                    statsCodeAtc++;
                    codeAtcMap.insert(std::make_pair(co.value, co));
                }
                else if (codeType == "DOSISTYP") {
                    statsCodeDOSISTYP++;
                }
                else if (codeType == "DOSISUNIT") {
                    statsCodeDOSISUNIT++;
                    codeDosisUnitMap.insert(std::make_pair(co.value, co));
                }
                else if (codeType == "EVIDENZ") {
                    statsCodeEVIDENZ++;
                }
                else if (codeType == "ROA") {
                    statsCodeRoa++;
                    
                    // Both the following are still unused
                    // One of them will be used to fetch the localized description if required
                    // So far we are just using the ROA from the cases struct instead.
                    codeRoaCodeSet.insert(co.value);
                    codeRoaVec.push_back(co);
                }
                else if (codeType == "ZEIT") {
                    statsCodeZEIT++;
                }

#if 0
                std::clog
                << basename((char *)__FILE__) << ":" << __LINE__
                << ", i: " << i++
                << ", # children: " << v.second.size()
                << ", CodeValue <" << co.value << ">"
                << std::endl;
#endif
            }
        } // FOREACH Codes

        i = 0;
        BOOST_FOREACH(pt::ptree::value_type &v, tree.get_child("SwissPedDosePublication.Dosages")) {
            if (v.first == "Dosage") {
                dosageCaseIDSet.insert(v.second.get("CaseID", ""));
                
                _dosage dos;
                dos.key = v.second.get("DosageKey", "");

                dos.ageFrom = v.second.get("AgeFrom", "");
                dos.ageFromUnit = v.second.get("AgeFromUnit", "");
                dos.ageTo = v.second.get("AgeTo", "");
                dos.ageToUnit = v.second.get("AgeToUnit", "");

                dos.ageWeightRelation = v.second.get("AgeWeightRelation", "");
                dos.weightFrom = v.second.get("WeightFrom", "");
                dos.weightTo = v.second.get("WeightTo", "");

                dos.doseLow = v.second.get("LowerDoseRange", "");
                dos.doseHigh = v.second.get("UpperDoseRange", "");
                dos.doseUnit = v.second.get("DoseRangeUnit", "");
                dos.doseUnitRef1 = v.second.get("DoseRangeReferenceUnit1", "");
                dos.doseUnitRef2 = v.second.get("DoseRangeReferenceUnit2", "");

                dos.dailyRepetitionsLow = v.second.get("LowerRangeDailyRepetitions", "");
                dos.dailyRepetitionsHigh = v.second.get("UpperRangeDailyRepetitions", "");

                dos.maxSingleDose = v.second.get("MaxSingleDose", "");
                dos.maxSingleDoseUnit = v.second.get("MaxSingleDoseUnit", "");
                dos.maxSingleDoseUnitRef1 = v.second.get("MaxSingleDoseReferenceUnit1", "");
                dos.maxSingleDoseUnitRef2 = v.second.get("MaxSingleDoseReferenceUnit2", "");

                dos.maxDailyDose = v.second.get("MaxDailyDose", "");
                dos.maxDailyDoseUnit = v.second.get("MaxDailyDoseUnit", "");
                dos.maxDailyDoseUnitRef1 = v.second.get("MaxDailyDoseReferenceUnit1", "");
                dos.maxDailyDoseUnitRef2 = v.second.get("MaxDailyDoseReferenceUnit2", "");

                if (language == "de")
                    dos.remarks = v.second.get("RemarksD", "");
                else if (language == "fr")
                    dos.remarks = v.second.get("RemarksF", "");
                else if (language == "it")
                    dos.remarks = v.second.get("RemarksI", "");
                else //if (language == "en")
                    dos.remarks = v.second.get("RemarksE", "");

                dos.caseId = v.second.get("CaseID", "");
                dos.type = v.second.get("TypeOfCase", "");

                dosageVec.push_back(dos);
            }
        } // FOREACH Dosages
    } // try
    catch (std::exception &e) {
        std::cerr
        << basename((char *)__FILE__) << ":" << __LINE__
        << ", Error " << e.what()
        << std::endl;
    }
    
    std::clog
    << basename((char *)__FILE__) << ":" << __LINE__
    << std::endl
    << "Cases: " << statsCasesCount
    << ", # CaseID set: " << caseCaseIDSet.size()
    << ", # ATC Code set: " << caseAtcCodeSet.size()
    << ", # ROA Code set: " << caseRoaCodeSet.size()
    << std::endl
    << "Indications: " << statsIndicationsCount
    << ", # indications map: " << indicationMap.size()
    << std::endl
    << "Dosages: " << statsDosagesCount
    << ", set: " << dosageCaseIDSet.size()
    << ", vec: " << dosageVec.size()
    << std::endl
    << "Codes: " << statsCodesCount
    << "\n  <CodeType>\n\t_ALTERRELATION: " << statsCode_ALTERRELATION
    << "\n\t_FG: " << statsCode_FG
    << "\n\t_GEWICHT: " << statsCode_GEWICHT
    << "\n\tATC: " << statsCodeAtc << ", map: " << codeAtcMap.size()
    << "\n\tDOSISTYP: " << statsCodeDOSISTYP
    << "\n\tDOSISUNIT: " << statsCodeDOSISUNIT << ", map: " << codeDosisUnitMap.size()
    << "\n\tEVIDENZ: " << statsCodeEVIDENZ
    << "\n\tROA: " << statsCodeRoa << ", set: " << codeRoaCodeSet.size() << ", vec: " << codeRoaVec.size()
    << "\n\tZEIT: " << statsCodeZEIT
    << std::endl;
}
    
std::string getDescriptionByAtc(const std::string &atc)
{
    return codeAtcMap[atc].description;
}

// There could be multiple cases for the same ATC. Return a vector
void getCasesByAtc(const std::string &atc, std::vector<_case> &cases)
{
    for (auto c : caseVec) {
        if (c.atcCode == atc)
            cases.push_back(c);
    }
}
    
std::string getIndicationByKey(const std::string &key)
{
    return indicationMap[key].name;
}

//_dosage getDosageById(const std::string &id)
void getDosageById(const std::string &id, std::vector<_dosage> &dosages)
{
    for (auto d : dosageVec)
        if (d.caseId == id)
            dosages.push_back(d);
}

std::string getHtmlByAtc(const std::string atc)
{
    std::string html;
    std::vector<_case> cases;
    PED::getCasesByAtc(atc, cases);
    
    if (cases.empty()) {
        statsCasesForAtcNotFoundCount++;
        return {};  // empty string
    }

    statsCasesForAtcFoundCount++;

    html.clear();

    for (auto ca : cases) {
        auto description = PED::getDescriptionByAtc(atc);
        auto indication = PED::getIndicationByKey(ca.indicationKey);

        html += "<br>\n" + description + "<br>\n";
        html += "\nATC-Code: " + atc + "<br>\n";
        html += "Indication: " + indication + "<br>\n";

        std::vector<_dosage> dosages;
        PED::getDosageById(ca.caseId, dosages);

#define TAG_TABLE_L     "<table class=\"s14\">"
#define TAG_TABLE_R     "</table>"
#define TAG_TD_L        "<td class=\"s13\"><p class=\"s11\">"
#define TAG_TD_R        "</p><div class=\"s12\"/></td>"

#ifdef WITH_SEPARATE_TABLE_HEADER
#define TAG_TH_L        "<th>"
#define TAG_TH_R        "</th>"
#else
#define TAG_TH_L        TAG_TD_L
#define TAG_TH_R        TAG_TD_R
#endif
        
#define NUM_COLUMNS     7

#if 1
        std::string tableColGroup("<col span=\"7\" style=\"background-color: #EEEEEE; padding-right: 5px; padding-left: 5px\"/>");
#else
        std::string tableColGroup;
        for (int i=0; i<NUM_COLUMNS; i++) {
            // TODO: width
            //tableColGroup += "<col style=\"width:33.333336%25;background-color: #EEEEEE; padding-right: 5px; padding-left: 5px\"/>";
            tableColGroup += "<col style=\"background-color: #EEEEEE; padding-right: 5px; padding-left: 5px\"/>";
        }
#endif

        tableColGroup = "<colgroup>" + tableColGroup + "</colgroup>";

        std::string tableHeader;
        tableHeader.clear();

        std::string tableBody;
        tableBody.clear();
        
        if (dosages.size() > 0) {
            tableHeader += TAG_TH_L + std::string("Age") + TAG_TH_R;
            tableHeader += TAG_TH_L + std::string("Weight") + TAG_TH_R;
            tableHeader += TAG_TH_L + std::string("Type of use") + TAG_TH_R;
            tableHeader += TAG_TH_L + std::string("Dosage") + TAG_TH_R;
            tableHeader += TAG_TH_L + std::string("Daily repetitions") + TAG_TH_R;
            tableHeader += TAG_TH_L + std::string("ROA") + TAG_TH_R;
            tableHeader += TAG_TH_L + std::string("Max. daily dose") + TAG_TH_R;
            tableHeader += "\n"; // for readability

            tableHeader = "<tr>" + tableHeader + "</tr>";
#ifdef WITH_SEPARATE_TABLE_HEADER
            tableHeader = "<thead>" + tableHeader + "</thead>";
#else
            tableBody += tableHeader;
#endif
        }

        for (auto dosage : dosages) {
            std::string tableRow;
            tableRow += TAG_TD_L;
            tableRow += dosage.ageFrom + dosage.ageFromUnit;
            tableRow += " to " + dosage.ageTo + dosage.ageToUnit;
            if (!dosage.ageWeightRelation.empty())
                tableRow += " " + dosage.ageWeightRelation;
            tableRow += TAG_TD_R;

            tableRow += TAG_TD_L;
            tableRow += dosage.weightFrom;
            if (dosage.weightFrom != dosage.weightTo)
                tableRow += " to " + dosage.weightTo;
            tableRow += " kg";
            tableRow += TAG_TD_R;

            tableRow += TAG_TD_L;
            tableRow += dosage.type;
            tableRow += TAG_TD_R;

            tableRow += TAG_TD_L;
            tableRow += dosage.doseLow;
            if (dosage.doseLow != dosage.doseHigh)
                tableRow += " - " + dosage.doseHigh;
            tableRow += " " + getAbbreviation(dosage.doseUnit);
            if (!dosage.doseUnitRef1.empty())
                tableRow += "/" + getAbbreviation(dosage.doseUnitRef1);
            if (!dosage.doseUnitRef2.empty())
                tableRow += "/" + getAbbreviation(dosage.doseUnitRef2);
            tableRow += TAG_TD_R;

            tableRow += TAG_TD_L;
            tableRow += dosage.dailyRepetitionsLow;
            if (dosage.dailyRepetitionsLow != dosage.dailyRepetitionsHigh)
                tableRow += " - " + dosage.dailyRepetitionsHigh;
            tableRow += + " x daily";
            tableRow += TAG_TD_R;

            tableRow += TAG_TD_L;
            tableRow += ca.RoaCode;
            tableRow += TAG_TD_R;

            tableRow += TAG_TD_L;
            tableRow += dosage.maxDailyDose + " " + getAbbreviation(dosage.maxDailyDoseUnit);
            if (!dosage.maxDailyDoseUnitRef1.empty())
                tableRow += "/" + getAbbreviation(dosage.maxDailyDoseUnitRef1);
            if (!dosage.maxDailyDoseUnitRef2.empty())
                tableRow += "/" + getAbbreviation(dosage.maxDailyDoseUnitRef2);
            tableRow += TAG_TD_R;

            tableRow += "\n";  // for readability

            tableRow = "<tr>" + tableRow + "</tr>";
            tableBody += tableRow;
        } // for dosages
        
        tableBody = "<tbody>" + tableBody + "</tbody>";

        std::string table = tableColGroup;
#ifdef WITH_SEPARATE_TABLE_HEADER
        table += tableHeader + tableBody;
#else
        table += tableBody;
#endif
        table = TAG_TABLE_L + table + TAG_TABLE_R;

        html += table;
    } // for cases

    html = "<p>" + html + "</p>";

    return html;
}

void showPedDoseByAtc(const std::string atc)
{
    std::vector<_case> cases;
    PED::getCasesByAtc(atc, cases);
    
    if (cases.empty()) {
        std::cout << "No cases for ATC: " << atc << std::endl;
        return;
    }

    std::cout << "Ped Dose, ATC: " << atc << std::endl;

    for (auto ca : cases) {
        auto description = PED::getDescriptionByAtc(atc);
        auto indication = PED::getIndicationByKey(ca.indicationKey);
        
        std::vector<_dosage> dosages;
        PED::getDosageById(ca.caseId, dosages);
        
        std::cout
        << "\t caseId: " << ca.caseId
        << "\n\t\t " << description << " (" << ca.RoaCode << ")"
        << "\n\t\t indication: " << indication
        << std::endl;

        for (auto dosage : dosages) {
            std::cout
            << "\t\t dosage recommendation: " << dosage.key
            << "\n\t\t\t age: " << dosage.ageFrom << " " << dosage.ageFromUnit
            << ", to: " << dosage.ageTo << " " << dosage.ageToUnit

            << "\n\t\t\t dosage: " << dosage.doseLow << " - " << dosage.doseHigh << " " << dosage.doseUnit << "/" << dosage.doseUnitRef1 << "/" << dosage.doseUnitRef2

            << "\n\t\t\t daily repetitions: " << dosage.dailyRepetitionsLow << " - " << dosage.dailyRepetitionsHigh
            
            << "\n\t\t\t max single dose: " << dosage.maxSingleDose << " " << dosage.maxSingleDoseUnit << "/" << dosage.maxSingleDoseUnitRef1 << "/" << dosage.maxSingleDoseUnitRef2
            
            << "\n\t\t\t max daily dose: " << dosage.maxDailyDose << " " << dosage.maxDailyDoseUnit << "/" << dosage.maxDailyDoseUnitRef1 << "/" << dosage.maxDailyDoseUnitRef2;

            if (!dosage.remarks.empty())
                std::cout << "\n\t\t\t remarks: " << dosage.remarks;

            std::cout << std::endl;
        }
    }
}
    
void printStats()
{
    std::cout
    << "PED ATC with cases: " << statsCasesForAtcFoundCount
    << ", ATC without cases: " << statsCasesForAtcNotFoundCount
    << std::endl;
}

}
