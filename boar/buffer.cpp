/* Boar - Boar is a toolkit to modify text files.
* Copyright (C) 2017 Katsuya Iida. All rights reserved.
*/

#include "stdafx.h"

#include "boar.h"
#include "buffer.h"
#include "cursor.h"

namespace boar
{
    template<>
    void Buffer<char>::Open(const char16_t* fileName)
    {
        std::ifstream fin;
        fin.open((char*)FromUnicode(fileName).c_str());
        if (fin.is_open())
        {
            Cursor<char> cursor(*this);
            std::string line;
            while (std::getline(fin, line))
            {
                std::string line(line.begin(), line.end());
                line += '\n';
                cursor.Append(line.c_str(), line.c_str() + line.length());
            }
        }
    }
}
