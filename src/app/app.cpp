/*
 * File: app.cpp
 *
 * Copyright (C) 2016 Richard Eliáš <richard.elias@matfyz.cz>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 * USA.
 */


#include "app.hpp"
#include "utils.hpp"
#include "mapping.hpp"
#include "tree_matcher.hpp"
#include "extractor.hpp"
#include "document_writer.hpp"
#include "compact.hpp"
#include "overlap_checks.hpp"
#include "rted.hpp"
#include "gted.hpp"

#define ARGS_HELP                           {"-h", "--help"}
#define ARGS_TARGET_STRUCTURE               {"-gs", "--target-structure"}
#define ARGS_TEMPLATE_STRUCTURE             {"-ts", "--template-structure"}
#define ARGS_TEMPLATE_STRUCTURE_FILE_TYPE   "--file-format"
#define ARGS_ALL                            {"-a", "--all"}
#define ARGS_ALL_OVERLAPS                   "--overlaps"
#define ARGS_TED                            {"-t", "--ted"}
#define ARGS_DRAW                           {"-d", "--draw"}
#define ARGS_DRAW_OVERLAPS                  "--overlaps"
#define ARGS_VERBOSE                        {"-v", "--verbose"}
#define ARGS_DEBUG                          {"--debug"}

#define COLORED_FILENAME_EXTENSION          ".colored"



using namespace std;


struct app::arguments
{
    rna_tree templated;
    rna_tree matched;

    struct
    {
        bool run = false;
        bool overlap_checks = false;
        string file;
    } all;
    struct
    {
        bool run = false;
        string mapping;
    } ted;
    struct
    {
        bool run = false;
        bool overlap_checks = false;
        string mapping;
        string file;
    } draw;

public:
    /**
     * parse arguments
     */
    static arguments parse(
                const std::vector<std::string>& args);

private:
    arguments() = default;
};


void app::run(
                std::vector<std::string> args)
{
    APP_DEBUG_FNAME;

    args.push_back("");
    run(arguments::parse(args));
}


void app::run(
                arguments args)
{
    APP_DEBUG_FNAME;

    INFO("BEG: APP");

    print(args);
    bool rted = args.all.run || args.ted.run;
    bool draw = args.all.run || args.draw.run;
    bool overlaps = args.all.overlap_checks || args.draw.overlap_checks;
    mapping map;
    string img_out = args.all.file;

    map = run_ted(args.templated, args.matched, rted, args.ted.mapping);

    if (args.draw.run)
    {
        assert(!args.draw.mapping.empty());
        map = load_mapping_table(args.draw.mapping);
        img_out = args.draw.file;
    }

    run_drawing(args.templated, args.matched, map, draw, overlaps, img_out);

    INFO("END: APP");
}

mapping app::run_ted(
                rna_tree& templated,
                rna_tree& matched,
                bool run,
                const std::string& mapping_file)
{
    APP_DEBUG_FNAME;

    try
    {
        mapping mapping;

        if (run)
        {
            rted r(templated, matched);
            r.run();

            gted g(templated, matched);
            g.run(r.get_strategies());

            mapping = g.get_mapping();

            if (!mapping_file.empty())
                save_tree_mapping_table(mapping_file, mapping);
        }
        else
        {
            INFO("skipping rted run, returning default mapping");
        }

        return mapping;
    }
    catch (const my_exception& e)
    {
        throw aplication_error("Tree-edit-distance computation failed: %s", e).with(ERROR_TED);
    }
}

void app::run_drawing(
                rna_tree& templated,
                rna_tree& matched,
                const mapping& mapping,
                bool run,
                bool run_overlaps,
                const std::string& file)
{
    APP_DEBUG_FNAME;

    try
    {
        if (!run)
        {
            INFO("skipping draw run");
            return;
        }

        templated = matcher(templated, matched).run(mapping);
        compact(templated).run();

        save(file, templated, run_overlaps);
    }
    catch (const my_exception& e)
    {
        throw aplication_error("Drawing structure failed: %s", e).with(ERROR_DRAW);
    }
}

void app::save(
                const std::string& filename,
                rna_tree& rna,
                bool overlap)
{
    APP_DEBUG_FNAME;

    overlap_checks::overlaps overlaps;
    if (overlap)
        overlaps = overlap_checks().run(rna);

    for (bool colored : {true, false})
    {
        for (auto& writer : document_writer::get_writers(colored))
        {
            string file = colored ? filename + COLORED_FILENAME_EXTENSION : filename;
            writer->init(file, rna.begin());
            writer->print(writer->get_rna_formatted(rna));

            for (const auto& p : overlaps)
                writer->print(writer->get_circle_formatted(p.centre, p.radius));
        }
    }

    if (overlap)
    {
        INFO("Overlaps computed: found %s in rna %s", overlaps.size(), rna.name());
    }
    else
    {
        INFO("Overlaps computation was skipped for %s", rna.name());
    }
}




rna_tree app::create_matched(
                const std::string& fastafile)
{
    APP_DEBUG_FNAME;

    try
    {
        fasta f = read_fasta_file(fastafile);
        return rna_tree(f.brackets, f.labels, f.id);
    }
    catch (const my_exception& e)
    {
        throw aplication_error("Creating target rna failed: %s", e).with(ERROR_ARGUMENTS);
    }
}

rna_tree app::create_templated(
                const std::string& templatefile,
                const std::string& templatetype,
                const std::string& fastafile)
{
    APP_DEBUG_FNAME;

    try
    {
        extractor_ptr doc = extractor::get_extractor(templatefile, templatetype);
        fasta f = read_fasta_file(fastafile);
        return rna_tree(f.brackets, doc->labels, doc->points, f.id);
    }
    catch (const my_exception& e)
    {
        throw aplication_error("Creating templated rna failed: %s", e).with(ERROR_ARGUMENTS);
    }
}




void app::usage(
                const string& appname)
{
    LOGGER_PRIORITY_ON_FUNCTION(INFO);

    char endl = '\n';

    auto get_args = [](vector<string> args)
    {
        assert(!args.empty());

        ostringstream out;
        for (const string& a : args)
            out << a << "|";
        string str = out.str();
        // delete last '|'
        return str.substr(0, str.size() - 1);
    };

    logger.info_stream()
        << "usage():"
        << endl
        << endl
        << appname
            << " [" << get_args(ARGS_HELP) << "]"
            << endl
        << appname
            << " [OPTIONS]"
            << " <" << get_args(ARGS_TARGET_STRUCTURE) << ">"
                << " DBN_FILE"
            << " <" << get_args(ARGS_TEMPLATE_STRUCTURE) << ">"
                << " [" << ARGS_TEMPLATE_STRUCTURE_FILE_TYPE << " FILE_FORMAT]"
                << " IMAGE_FILE DBN_FILE"
            << endl
        << endl
        << "OPTIONS:" << endl
        << "\t[" << get_args(ARGS_ALL)
            << "] [" << ARGS_ALL_OVERLAPS << "] FILE_OUT"
        << endl
        << "\t[" << get_args(ARGS_TED) << "] FILE_MAPPING_OUT"
        << endl
        << "\t[" << get_args(ARGS_DRAW)
            << "] [" << ARGS_DRAW_OVERLAPS << "] FILE_MAPPING_IN FILE_OUT"
        << endl
        << "\t[" << get_args(ARGS_VERBOSE) << "]"
        << endl;
}

void app::print(
                const arguments& args)
{
    APP_DEBUG_FNAME;

    INFO("ARGUMENTS:\n"
            "templated: %s: %s\n"
            "matched: %s: %s\n"
            "all:\n"
                "\trun=%s\n"
                "\timage-file=%s\n"
                "\toverlaps=%s\n"
            "ted:\n"
                "\trun=%s\n"
                "\tmapping-file=%s\n"
            "draw:\n"
                "\trun=%s\n"
                "\toverlaps=%s\n"
                "\tmapping-file=%s\n"
                "\timage-file=%s",
            args.templated.name(), args.templated.print_tree(false),
            args.matched.name(), args.matched.print_tree(false),
            args.all.run, args.all.file, args.all.overlap_checks,
            args.ted.run, args.ted.mapping,
            args.draw.run, args.draw.overlap_checks, args.draw.mapping, args.draw.file);
}


/* static */ app::arguments app::arguments::parse(
                const std::vector<std::string>& args)
{
    APP_DEBUG_FNAME;

    try
    {
        size_t i;
        string arg;
        arguments a;

        auto show_usage_and_exit =
            [&args]()
            {   
                app::usage(args.at(0));
                exit(0);
            };
        auto nextarg =
            [&args, &i]()
            {
                if (i + 1 < args.size())
                    return args.at(i + 1);
                else
                    return args.back();
            };
        auto is_argument =
            [&arg](const std::vector<std::string>& arguments)
            {
                for (const string& a : arguments)
                    if (a == arg)
                        return true;
                return false;
            };

        if (args.size() == 2)
        {
            DEBUG("No arguments");
            show_usage_and_exit();
        }


        for (i = 1; i < args.size(); ++i)
        {
            arg = args.at(i);
            if (arg.empty())
                continue;

            if (is_argument(ARGS_HELP))
            {
                DEBUG("arg help");
                show_usage_and_exit();
            }
            else if (is_argument(ARGS_TARGET_STRUCTURE))
            {
                DEBUG("arg match-tree");
                string fastafile = args.at(i + 1);
                a.matched = app::create_matched(fastafile);
                ++i;
                continue;
            }
            else if (is_argument(ARGS_TEMPLATE_STRUCTURE))
            {
                DEBUG("arg template-tree");
                string templatefile, fastafile;
                string templatetype = "crw";
                if (nextarg() == ARGS_TEMPLATE_STRUCTURE_FILE_TYPE)
                {
                    templatetype = args.at(i + 2);
                    i += 2;
                }
                templatefile = args.at(i + 1);
                fastafile = args.at(i + 2);
                a.templated = app::create_templated(templatefile, templatetype, fastafile);
                i += 2;
            }
            else if (is_argument(ARGS_ALL))
            {
                DEBUG("arg all");
                a.all.run = true;
                while (true)
                {
                    if (nextarg() == ARGS_ALL_OVERLAPS)
                    {
                        a.all.overlap_checks = true;
                        i += 1;
                    }
                    else
                        break;
                }
                a.all.file = args.at(i + 1);
                ++i;
            }
            else if (is_argument(ARGS_TED))
            {
                DEBUG("arg ted");
                a.ted.run = true;
                a.ted.mapping = args.at(i + 1);
                i += 1;
            }
            else if (is_argument(ARGS_DRAW))
            {
                DEBUG("arg draw");
                a.draw.run = true;
                while (true)
                {
                    if (nextarg() == ARGS_DRAW_OVERLAPS)
                    {
                        a.draw.overlap_checks = true;
                        i += 1;
                    }
                    else
                        break;
                }
                a.draw.mapping = args.at(i + 1);
                a.draw.file = args.at(i + 2);
                i += 2;
            }
            else if (is_argument(ARGS_VERBOSE))
            {
                logger.set_priority(logger::INFO);
                INFO("Enabled verbose mode");
            }
            else if (is_argument(ARGS_DEBUG))
            {
                logger.set_priority(logger::TRACE);
                INFO("Enabled trace mode");
            }
            else
            {
                throw wrong_argument_exception("Wrong parameter no.%i: '%s'; try running %s --help for more arguments details",
                        i, arg, args[0]);
            }
        }

        if (a.templated == rna_tree() || a.matched == rna_tree())
            throw wrong_argument_exception("RNA structures are missing, try running %s --help for more arguments details", args[0]);

        return a;
    }
    catch (const my_exception& e)
    {
        throw aplication_error("Error while parsing arguments: %s", e).with(ERROR_ARGUMENTS);
    }
}


