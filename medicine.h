//
//  medicine.h
//  cpp2sqlite
//
//  Created by Alex Bettarini on 16 Jan 2019
//

#ifndef medicine_h
#define medicine_h

#include <vector>

namespace AIPS
{
    
struct Medicine {
    std::string title;
    std::string atc;
    std::string subst;
};

typedef std::vector<Medicine> MedicineList;

}

#endif /* medicine_h */
