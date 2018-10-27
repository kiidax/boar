/* bigtext - bigtext is a collection of tools to process large text files.
* Copyright (C) 2017 Katsuya Iida. All rights reserved.
*/

#include "stdafx.h"

#include "bigtext.h"
#include "filesource.h"
#include "count.h"
#include "sample.h"

namespace bigtext
{
    namespace fs = boost::filesystem;

    static int SampleUsage()
    {
        std::wcout << "Usage: bigtext sample [OPTION]... INPUTFILE... [[-o|-n LINES|-r RATE] OUTPUTFILE]..." << std::endl;
        std::wcout << std::endl;
        std::wcout << " -c N       shuffle output files with N interleaving" << std::endl;
        std::wcout << " -f         force overwrite output files" << std::endl;
        std::wcout << " -h         show this help message" << std::endl;
        std::wcout << " -q         quick mode" << std::endl;
        std::wcout << " -s         shuffle output files" << std::endl;
        std::wcout << " INPUTFILE  input file" << std::endl;
        std::wcout << " -o         sample all lines" << std::endl;
        std::wcout << " -n LINES   sample around n lines" << std::endl;
        std::wcout << " -r RATE    sampling rate. Probability (0.0,1.0] or percent (0,100]%" << std::endl;
        std::wcout << " OUTPUTFILE output file" << std::endl;
        return 0;
    }

    static bool HasNumberOfLines(const std::vector<SampleOutputSpec> &outputSpecList)
    {
        return std::any_of(outputSpecList.cbegin(), outputSpecList.cend(), [](auto &spec) { return spec.numberOfLines != 0; });
    }

    static bool HasSampleRate(const std::vector<SampleOutputSpec> &outputSpecList)
    {
        return std::any_of(outputSpecList.cbegin(), outputSpecList.cend(), [](auto &spec) { return spec.numberOfLines == 0 && spec.rate < 1.0; });
    }

    static bool HasSampleAll(const std::vector<SampleOutputSpec> &outputSpecList)
    {
        return std::any_of(outputSpecList.cbegin(), outputSpecList.cend(), [](auto &spec) { return spec.numberOfLines == 0 && spec.rate == 1.0; });
    }

    static void ConvertToRate(std::vector<SampleOutputSpec> &outputSpecList, double totalNumberOfLines)
    {
        for (auto &spec : outputSpecList)
        {
            if (spec.numberOfLines != 0)
            {
                spec.rate = static_cast<double>(spec.numberOfLines) / totalNumberOfLines;
                spec.numberOfLines = 0;
                std::wcout << spec.fileName.native() << "\tRate\t" << spec.rate << std::endl;
            }
        }
    }

    static void ConvertToNumberOfLines(std::vector<SampleOutputSpec> &outputSpecList, double totalNumberOfLines)
    {
        for (auto &spec : outputSpecList)
        {
            if (spec.rate > 0.0)
            {
                spec.numberOfLines = static_cast<uintmax_t>(spec.rate * totalNumberOfLines);
                spec.rate = 0.0;
                std::wcout << spec.fileName.native() << "\tNumberOfLines\t" << spec.numberOfLines << std::endl;
            }
        }
    }

    template <typename CharT>
    static double GuessTotalNumberOfLines(const std::vector<fs::path> &inputFileNameList)
    {
        double totalNumberOfLines = 0;

        for (auto &fileName : inputFileNameList)
        {
            GuessLineInfo info = FileStatLines<CharT>(fileName);
            double estLineCount;
            if (info.isAccurate)
            {
                std::wcout << fileName.native() << "\tEstLineCount\t" << info.lineCount << std::endl;
                estLineCount = static_cast<double>(info.lineCount);
            }
            else
            {
                uintmax_t size = fs::file_size(fileName);
                estLineCount = info.avgLineSize == 0 ? 1.0 : (size / (sizeof(CharT) * info.avgLineSize));
                std::wcout << fileName.native() << "\tEstLineCount\t" << estLineCount << std::endl;
            }
            totalNumberOfLines += estLineCount;
        }

        return totalNumberOfLines;
    }

    static void DetermineBufferSize(const std::vector<fs::path> &inputFileNameList, uintmax_t &interleavingSize, size_t &maxBufferSize)
    {
        if (interleavingSize == 1)
        {
            maxBufferSize = 0;
        }
        else
        {
            uintmax_t memSize = GetPhysicalMemorySize();
            maxBufferSize = memSize * 8 / 10;

            if (interleavingSize == 0)
            {
                if (maxBufferSize == 0)
                {
                    interleavingSize = 1;
                }
                else
                {
                    uintmax_t size = 0;
                    for (auto &fileName : inputFileNameList)
                    {
                        size += fs::file_size(fileName);
                    }

                    interleavingSize = (size + maxBufferSize) / maxBufferSize;
                    assert(interleavingSize >= 1);
                }
            }
        }
    }

    int SampleCommand(int argc, wchar_t *argv[])
    {
        int optind = 1;
        uintmax_t interleavingSize = 0;
        bool forceOverwrite = false;
        bool shuffleOutput = false;
        bool hasOutputAll = false;
        bool quickMode = false;
        std::vector<fs::path> inputFileNameList;
        std::vector<SampleOutputSpec> outputSpecList;

        if (argc <= 1)
        {
            return SampleUsage();
        }

        while (optind < argc)
        {
            const wchar_t *p = argv[optind++];
            if (*p != '-')
            {
                // Input files start.
                optind--;
                break;
            }

            ++p;
            if (*p == '\0')
            {
                std::cerr << "An option is expected." << std::endl;
                return 1;
            }

            bool nextIsNumber = false;
            while (*p != '\0')
            {
                switch (*p)
                {
                case 'c':
                    nextIsNumber = true;
                    break;
                case 'f':
                    forceOverwrite = true;
                    break;
                case 'h':
                    return SampleUsage();
                case 'q':
                    quickMode = true;
                    break;
                case 's':
                    shuffleOutput = true;
                    break;
                case 'n':
                case 'o':
                case 'r':
                    std::wcerr << "No input files." << std::endl;
                    return 1;
                default:
                    std::wcerr << "Unknown option `" << *p << "'." << std::endl;
                    return 1;
                }
                ++p;

                if (nextIsNumber)
                {
                    if (*p == '\0')
                    {
                        if (optind >= argc)
                        {
                            std::wcerr << "Interlaving size is expected." << std::endl;
                            return 1;
                        }
                        p = argv[optind++];
                    }

                    if (!TryParseNumber(p, interleavingSize))
                    {
                        std::wcerr << "Invalid interleaving size." << std::endl;
                        return 1;
                    }
                    break;
                }
            }
        }

        if (interleavingSize > 0 && !shuffleOutput)
        {
            std::wcerr << "Interleaving size is allowed only in the shuffle mode." << std::endl;
            return 1;
        }

        while (optind < argc)
        {
            const wchar_t *p = argv[optind++];
            if (*p == '-')
            {
                // Output files start.
                optind--;
                break;
            }

            inputFileNameList.push_back(p);
        }

        if (inputFileNameList.size() == 0)
        {
            std::cerr << "No input files." << std::endl;
            return 1;
        }

        while (optind < argc)
        {
            const wchar_t *p = argv[optind++];
            bool nextIsRate = false;
            bool nextIsNumber = false;
            if (*p++ != '-')
            {
                std::wcerr << "-r, -n or -o is expected." << std::endl;
                return 1;
            }

            if (*p == '\0')
            {
                std::cerr << "An option is expected." << std::endl;
                return 1;
            }

            if (hasOutputAll)
            {
                std::wcerr << "Another output after -o option is not allowed." << std::endl;
                return 1;
            }

            switch (*p)
            {
            case 'n':
                nextIsNumber = true;
                break;
            case 'o':
                hasOutputAll = true;
                break;
            case 'r':
                nextIsRate = true;
                break;
            default:
                std::wcerr << "Unknown option `" << *p << "'." << std::endl;
                return 1;
            }
            p++;

            if (*p == '\0')
            {
                if (optind >= argc)
                {
                    if (nextIsRate)
                    {
                        std::wcerr << "Rate is expected." << std::endl;
                    }
                    else if (nextIsNumber)
                    {
                        std::wcerr << "Line number is expected." << std::endl;
                    }
                    else
                    {
                        std::wcerr << "Output file name is expected" << std::endl;
                    }
                    return 1;
                }
                p = argv[optind++];
            }

            double rate = 0.0;
            uintmax_t targetNumLines = 0;

            if (nextIsRate)
            {
                if (!TryParseRate(p, rate))
                {
                    std::wcerr << "Invalid rate `" << p << "'." << std::endl;
                    return 1;
                }
            }
            else if (nextIsNumber)
            {
                if (!TryParseNumber(p, targetNumLines))
                {
                    std::wcerr << "Invalid line number `" << p << "'." << std::endl;
                    return 1;
                }
            }

            if (nextIsRate || nextIsNumber)
            {
                if (optind >= argc)
                {
                    std::wcerr << "Output file name is expected" << std::endl;
                    return 1;
                }

                p = argv[optind++];
            }

            if (*p == '-')
            {
                std::wcerr << "Output file name is expected" << std::endl;
                return 1;
            }

            if (nextIsRate)
            {
                std::wcout << p << "\tTargetRate\t" << rate << std::endl;
                outputSpecList.emplace_back(p, rate);
            }
            else if (nextIsNumber)
            {
                std::wcout << p << "\tTargetLineCount\t" << targetNumLines << std::endl;
                outputSpecList.emplace_back(p, targetNumLines);
            }
            else
            {
                std::wcout << p << "\tTargetRate\t" << 1.0 << std::endl;
                outputSpecList.emplace_back(p);
            }
        }

        if (outputSpecList.size() == 0)
        {
            std::wcerr << "No output files." << std::endl;
            return 1;
        }

        // Verify all the input files exist
        if (!CheckInputFiles(inputFileNameList))
        {
            return 1;
        }

        if (!forceOverwrite)
        {
            // Verify none of the output files exists
            std::vector<fs::path> outputFileNameList;
            for (auto &spec : outputSpecList) outputFileNameList.push_back(spec.fileName);
            if (!CheckOutputFiles(outputFileNameList))
            {
                return 1;
            }
        }

        std::srand(static_cast<int>(std::time(nullptr)));

        boost::timer::cpu_timer timer;

        if (quickMode)
        {
            if (shuffleOutput)
            {
                std::wcerr << "-s is not allowed with the quick mode. The quick mode shuffles outputs." << std::endl;
                return 1;
            }

            if (inputFileNameList.size() != 1)
            {
                std::wcerr << "Only one input file is allowed." << std::endl;
                return 1;
            }

            if (HasSampleAll(outputSpecList))
            {
                std::wcerr << "Sampling all lines doesn't make sense with quick mode." << std::endl;
                return 1;
            }

            if (HasSampleRate(outputSpecList))
            {
                std::cout << "Target rate is specified. Guessing number of lines." << std::endl;
                double totalNumberOfLines = GuessTotalNumberOfLines<char>(inputFileNameList);
                ConvertToNumberOfLines(outputSpecList, totalNumberOfLines);
            }

            FileQuickSampleFileLines<char>(inputFileNameList[0], outputSpecList, shuffleOutput);
        }
        else if (shuffleOutput)
        {
            size_t maxBufferSize;
            DetermineBufferSize(inputFileNameList, interleavingSize, maxBufferSize);

            if (interleavingSize == 1)
            {
                FileShuffleLines<char>(inputFileNameList, outputSpecList);
            }
            else
            {
                FileShuffleLines<char>(inputFileNameList, outputSpecList, interleavingSize, maxBufferSize);
            }
        }
        else
        {
            if (HasNumberOfLines(outputSpecList))
            {
                std::cout << "Target number of lines is specified. Guessing number of lines." << std::endl;
                double totalNumberOfLines = GuessTotalNumberOfLines<char>(inputFileNameList);
                ConvertToRate(outputSpecList, totalNumberOfLines);
            }

            if (outputSpecList.size() == 1 && outputSpecList[0].numberOfLines == 0)
            {
                std::wcout << "Only one output without target number of lines. Using simple mode." << std::endl;
                assert(outputSpecList[0].numberOfLines == 0);
                FileLineSample<char>(inputFileNameList, outputSpecList[0].rate, outputSpecList[0].fileName);
            }
            else
            {
                FileLineSample<char>(inputFileNameList, outputSpecList);
            }
        }

        std::cerr << timer.format() << std::endl;

        return 0;
    }
}