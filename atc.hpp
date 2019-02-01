//
//  atc.hpp
//  cpp2sqlite
//
//  ©ywesee GmbH -- all rights reserved
//  License GPLv3.0 -- see License File
//  Created by Alex Bettarini on 30 Jan 2019
//

#ifndef atc_hpp
#define atc_hpp

namespace ATC
{
    void parseTXT(const std::string &filename,
                  const std::string &language,
                  bool verbose);

    void validate(const std::string &regnrs,
                  std::string &name);
    
    std::string getTextFromAtc(std::string atc);
}

#endif /* atc_hpp */