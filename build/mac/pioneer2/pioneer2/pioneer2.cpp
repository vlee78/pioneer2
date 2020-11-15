//
//  pioneer2.cpp
//  pioneer2
//
//  Created by 李胜存 on 2020/11/15.
//  Copyright © 2020 李胜存. All rights reserved.
//

#include <iostream>
#include "pioneer2.hpp"
#include "pioneer2Priv.hpp"

void pioneer2::HelloWorld(const char * s)
{
    pioneer2Priv *theObj = new pioneer2Priv;
    theObj->HelloWorldPriv(s);
    delete theObj;
};

void pioneer2Priv::HelloWorldPriv(const char * s) 
{
    std::cout << s << std::endl;
};

