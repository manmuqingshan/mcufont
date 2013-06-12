#include "bdf_import.hh"
#include "c_export.hh"
#include "datafile.hh"
#include "encode.hh"
#include "freetype_import.hh"
#include "optimize.hh"
#include <vector>
#include <string>
#include <set>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <ctime>
#include <map>

static std::string strip_extension(std::string filename)
{
    size_t pos = filename.find_last_of('.');
    
    if (pos == std::string::npos)
    {
        return filename;
    }
    else
    {
        return filename.substr(0, pos);
    }
}

static std::unique_ptr<DataFile> load_dat(std::string src)
{
    std::ifstream infile(src);
    
    if (!infile.good())
    {
        std::cerr << "Could not open " << src << std::endl;
        return nullptr;
    }
    
    std::unique_ptr<DataFile> f = DataFile::Load(infile);
    if (!f)
    {
        std::cerr << "Invalid format for .dat file: " << src << std::endl;
        return nullptr;
    }
    
    return f;
}

static bool save_dat(std::string dest, DataFile *f)
{
    std::ofstream outfile(dest);
    
    if (!outfile.good())
    {
        std::cerr << "Could not open " << dest << std::endl;
        return false;
    }
    
    f->Save(outfile);
    
    if (!outfile.good())
    {
        std::cerr << "Could not write to " << dest << std::endl;
        return false;
    }
    
    return true;
}

enum status_t
{
    STATUS_OK = 0, // All good
    STATUS_INVALID = 1, // Invalid command or args
    STATUS_ERROR = 2 // Error when executing command
};

static status_t cmd_import_ttf(const std::vector<std::string> &args)
{
    if (args.size() != 3 && args.size() != 4)
        return STATUS_INVALID;
    
    std::string src = args.at(1);
    int size = std::stoi(args.at(2));
    bool bw = (args.size() == 4 && args.at(3) == "bw");
    std::string dest = strip_extension(src) + std::to_string(size) + (bw ? "bw" : "") + ".dat";
    std::ifstream infile(src);
    
    if (!infile.good())
    {
        std::cerr << "Could not open " << src << std::endl;
        return STATUS_ERROR;
    }
    
    std::cout << "Importing " << src << " to " << dest << std::endl;
    
    std::unique_ptr<DataFile> f = LoadFreetype(infile, size, bw);
    
    init_dictionary(*f);
    
    if (!save_dat(dest, f.get()))
        return STATUS_ERROR;
    
    std::cout << "Done: " << f->GetGlyphCount() << " unique glyphs." << std::endl;
    return STATUS_OK;
}

static status_t cmd_import_bdf(const std::vector<std::string> &args)
{
    if (args.size() != 2)
        return STATUS_INVALID;
    
    std::string src = args.at(1);
    std::string dest = strip_extension(args.at(1)) + ".dat";
    std::ifstream infile(src);
    
    if (!infile.good())
    {
        std::cerr << "Could not open " << src << std::endl;
        return STATUS_ERROR;
    }
    
    std::cout << "Importing " << src << " to " << dest << std::endl;
    
    std::unique_ptr<DataFile> f = LoadBDF(infile);
    
    init_dictionary(*f);
    
    if (!save_dat(dest, f.get()))
        return STATUS_ERROR;
    
    std::cout << "Done: " << f->GetGlyphCount() << " unique glyphs." << std::endl;
    return STATUS_OK;
}

static status_t cmd_export(const std::vector<std::string> &args)
{
    if (args.size() != 3)
        return STATUS_INVALID;
    
    std::string src = args.at(1);
    std::string dst = args.at(2);
    std::unique_ptr<DataFile> f = load_dat(src);
    
    if (!f)
        return STATUS_ERROR;
    
    {
        std::ofstream header(dst + ".h");
        write_header(header, dst, *f);
        std::cout << "Wrote " << dst << ".h" << std::endl;
    }
    
    {
        std::ofstream source(dst + ".c");
        write_source(source, dst, *f);
        std::cout << "Wrote " << dst << ".c" << std::endl;
    }
    
    return STATUS_OK;
}

static status_t cmd_filter(const std::vector<std::string> &args)
{
    if (args.size() < 3)
        return STATUS_INVALID;
    
    std::set<int> allowed;
    
    // Parse arguments
    for (size_t i = 2; i < args.size(); i++)
    {
        std::string s = args.at(i);
        size_t pos = s.find('-');
        if (pos == std::string::npos)
        {
            // Single char
            allowed.insert(std::stoi(s, nullptr, 0));
        }
        else
        {
            // Range
            int start = std::stoi(s.substr(0, pos), nullptr, 0);
            int end = std::stoi(s.substr(pos + 1), nullptr, 0);
            
            for (int j = start; j <= end; j++)
            {
                allowed.insert(j);
            }
        }
    }
    
    std::string src = args.at(1);
    std::unique_ptr<DataFile> f = load_dat(src);
    if (!f)
        return STATUS_ERROR;
    
    std::cout << "Font originally had " << f->GetGlyphCount() << " glyphs." << std::endl;
    
    // Filter the glyphs
    std::vector<DataFile::glyphentry_t> newglyphs;
    for (size_t i = 0; i < f->GetGlyphCount(); i++)
    {
        DataFile::glyphentry_t g = f->GetGlyphEntry(i);
        
        for (size_t j = 0; j < g.chars.size(); j++)
        {
            if (!allowed.count(g.chars.at(j)))
            {
                g.chars.erase(g.chars.begin() + j);
                j--;
            }
        }
        
        if (g.chars.size())
        {
            newglyphs.push_back(g);
        }
    }
    
    f.reset(new DataFile(f->GetDictionary(), newglyphs, f->GetFontInfo()));
    std::cout << "After filtering, " << f->GetGlyphCount() << " glyphs remain." << std::endl;
    
    if (!save_dat(src, f.get()))
        return STATUS_ERROR;
    
    return STATUS_OK;
}

static status_t cmd_size(const std::vector<std::string> &args)
{
    if (args.size() != 2)
        return STATUS_INVALID;
    
    std::string src = args.at(1);
    std::unique_ptr<DataFile> f = load_dat(src);
    
    if (!f)
        return STATUS_ERROR;
    
    size_t size = get_encoded_size(*f);
    
    std::cout << "Glyph count:       " << f->GetGlyphCount() << std::endl;
    std::cout << "Glyph bbox:        " << f->GetFontInfo().max_width << "x"
        << f->GetFontInfo().max_height << " pixels" << std::endl;
    std::cout << "Uncompressed size: " << f->GetGlyphCount() *
        f->GetFontInfo().max_width * f->GetFontInfo().max_height / 2
        << " bytes" << std::endl;
    std::cout << "Compressed size:   " << size << " bytes" << std::endl;
    std::cout << "Bytes per glyph:   " << size / f->GetGlyphCount() << std::endl;
    return STATUS_OK;
}

static status_t cmd_optimize(const std::vector<std::string> &args)
{
    if (args.size() != 2 && args.size() != 3)
        return STATUS_INVALID;
    
    std::string src = args.at(1);
    std::unique_ptr<DataFile> f = load_dat(src);
    
    if (!f)
        return STATUS_ERROR;
    
    size_t oldsize = get_encoded_size(*f);
    
    std::cout << "Original size is " << oldsize << " bytes" << std::endl;
    std::cout << "Press ctrl-C at any time to stop." << std::endl;
    std::cout << "Results are saved automatically after each iteration." << std::endl;
    
    int limit = 100;
    if (args.size() == 3)
    {
        limit = std::stoi(args.at(2));
    }
    
    if (limit > 0)
        std::cout << "Limit is " << limit << " iterations" << std::endl;
    
    int i = 0;
    time_t oldtime = time(NULL);
    while (!limit || i < limit)
    {
        optimize(*f);

        size_t newsize = get_encoded_size(*f);
        time_t newtime = time(NULL);
        
        int bytes_per_min = (oldsize - newsize) * 60 / (newtime - oldtime + 1);
        
        i++;
        std::cout << "iteration " << i << ", size " << newsize
                  << " bytes, speed " << bytes_per_min << " B/min"
                  << std::endl;
        
        {
            if (!save_dat(src, f.get()))
                return STATUS_ERROR;
        }
    }
    
    return STATUS_OK;
}

static status_t cmd_show_encoded(const std::vector<std::string> &args)
{
    if (args.size() != 2)
        return STATUS_INVALID;
    
    std::string src = args.at(1);
    std::unique_ptr<DataFile> f = load_dat(src);
    
    if (!f)
        return STATUS_ERROR;
    
    std::unique_ptr<encoded_font_t> e = encode_font(*f);

    int i = 0;
    for (encoded_font_t::rlestring_t d : e->rle_dictionary)
    {
        std::cout << "Dict RLE " << 24 + i++ << ": ";
        for (uint8_t v : d)
            std::cout << std::setfill('0') << std::setw(2) << std::hex << (int)v << " ";
        std::cout << std::endl;
    }
    
    for (encoded_font_t::refstring_t d : e->ref_dictionary)
    {
        std::cout << "Dict Ref " << 24 + i++ << ": ";
        for (uint8_t v : d)
            std::cout << std::setfill('0') << std::setw(2) << std::hex << (int)v << " ";
        std::cout << std::endl;
    }
    
    i = 0;
    for (encoded_font_t::refstring_t g : e->glyphs)
    {
        std::cout << "Glyph " << i++ << ": ";
        for (uint8_t v : g)
            std::cout << std::setfill('0') << std::setw(2) << std::hex << (int)v << " ";
        std::cout << std::endl;
    }
    
    return STATUS_OK;
}

static status_t cmd_show_glyph(const std::vector<std::string> &args)
{
    if (args.size() != 3)
        return STATUS_INVALID;
    
    std::string src = args.at(1);
    std::unique_ptr<DataFile> f = load_dat(src);
    
    if (!f)
        return STATUS_ERROR;
    
    size_t index = 0;
    if (args.at(2) == "largest")
    {
        std::unique_ptr<encoded_font_t> e = encode_font(*f);
        size_t maxlen = 0;
        size_t i = 0;
        for (encoded_font_t::refstring_t g : e->glyphs)
        {
            if (g.size() > maxlen)
            {
                maxlen = g.size();
                index = i;
            }
            i++;
        }
        
        std::cout << "Index " << index << ", length " << maxlen << std::endl;
    }
    else
    {
        index = strtol(args.at(2).c_str(), nullptr, 0);
    }
    
    if (index < 0 || index >= f->GetGlyphCount())
    {
        std::cerr << "No such glyph " << index << std::endl;
        return STATUS_ERROR;
    }
    
    std::cout << f->GlyphToText(index);
    return STATUS_OK;
}

static const char *usage_msg =
    "Usage: mcufont <command> [options] ...\n"
    "   import_ttf <ttffile> <size> [bw]   Import a .ttf font into a data file.\n"
    "   import_bdf <bdffile>               Import a .bdf font into a data file.\n"
    "   export <datfile> <basename>        Export to .c and .h source code.\n"
    "   filter <datfile> <range> ...       Remove everything except specified characters.\n"
    "   size <datfile>                     Check the encoded size of the data file.\n"
    "   optimize <datfile>                 Perform an optimization pass on the data file.\n"
    "   show_encoded <datfile>             Show the encoded data for debugging.\n"
    "   show_glyph <datfile> <index>       Show the glyph at index.\n";

typedef status_t (*cmd_t)(const std::vector<std::string> &args);
static const std::map<std::string, cmd_t> command_list {
    {"import_ttf",      cmd_import_ttf},
    {"import_bdf",      cmd_import_bdf},
    {"export",          cmd_export},
    {"filter",          cmd_filter},
    {"size",            cmd_size},
    {"optimize",        cmd_optimize},
    {"show_encoded",    cmd_show_encoded},
    {"show_glyph",      cmd_show_glyph}
};

int main(int argc, char **argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++)
        args.push_back(argv[i]);
    
    status_t status = STATUS_INVALID;
    if (args.size() >= 1 && command_list.count(args.at(0)))
    {
        status = command_list.find(args.at(0))->second(args);
    }
    
    if (status == STATUS_INVALID)
    {
        std::cout << usage_msg << std::endl;
    }
    
    return status;
}