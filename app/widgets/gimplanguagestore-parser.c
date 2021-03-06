/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimplanguagestore-parser.c
 * Copyright (C) 2008, 2009  Sven Neumann <sven@gimp.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>

#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"

#include "widgets-types.h"

#include "config/gimpxmlparser.h"

#include "gimplanguagestore.h"
#include "gimplanguagestore-parser.h"

#include "gimp-intl.h"


typedef enum
{
  ISO_CODES_START,
  ISO_CODES_IN_ENTRIES,
  ISO_CODES_IN_ENTRY,
  ISO_CODES_IN_UNKNOWN
} IsoCodesParserState;

typedef struct
{
  IsoCodesParserState  state;
  IsoCodesParserState  last_known_state;
  gint                 unknown_depth;
  GimpLanguageStore   *store;
} IsoCodesParser;


static void  iso_codes_parser_start_element (GMarkupParseContext  *context,
                                             const gchar          *element_name,
                                             const gchar         **attribute_names,
                                             const gchar         **attribute_values,
                                             gpointer              user_data,
                                             GError              **error);
static void  iso_codes_parser_end_element   (GMarkupParseContext  *context,
                                             const gchar          *element_name,
                                             gpointer              user_data,
                                             GError              **error);

static void  iso_codes_parser_start_unknown (IsoCodesParser       *parser);
static void  iso_codes_parser_end_unknown   (IsoCodesParser       *parser);

static void  gimp_language_store_self_l10n  (GimpLanguageStore *store,
                                             const gchar       *lang,
                                             const gchar       *code);


static void
iso_codes_parser_init (void)
{
  static gboolean initialized = FALSE;

  if (initialized)
    return;

#ifdef G_OS_WIN32
  /*  on Win32, assume iso-codes is installed in the same location as GIMP  */
  bindtextdomain ("iso_639", gimp_locale_directory ());
#else
  bindtextdomain ("iso_639", ISO_CODES_LOCALEDIR);
#endif

  bind_textdomain_codeset ("iso_639", "UTF-8");

  initialized = TRUE;
}

gboolean
gimp_language_store_parse_iso_codes (GimpLanguageStore  *store,
                                     GError            **error)
{
#ifdef HAVE_ISO_CODES
  static const GMarkupParser markup_parser =
    {
      iso_codes_parser_start_element,
      iso_codes_parser_end_element,
      NULL,  /*  characters   */
      NULL,  /*  passthrough  */
      NULL   /*  error        */
    };

  GimpXmlParser   *xml_parser;
  gchar           *filename;
  gboolean         success;
  IsoCodesParser   parser = { 0, };

  g_return_val_if_fail (GIMP_IS_LANGUAGE_STORE (store), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  iso_codes_parser_init ();

  parser.store = g_object_ref (store);

  xml_parser = gimp_xml_parser_new (&markup_parser, &parser);

#ifdef G_OS_WIN32
  filename = g_build_filename (gimp_data_directory (),
                               "..", "..", "xml", "iso-codes", "iso_639.xml",
                               NULL);
#else
  filename = g_build_filename (ISO_CODES_LOCATION, "iso_639.xml", NULL);
#endif

  success = gimp_xml_parser_parse_file (xml_parser, filename, error);

  g_free (filename);

  gimp_xml_parser_free (xml_parser);
  g_object_unref (parser.store);

  return success;
#endif

  return TRUE;
}

static void
iso_codes_parser_entry (IsoCodesParser  *parser,
                        const gchar    **names,
                        const gchar    **values)
{
  const gchar *lang = NULL;
  const gchar *code = NULL;

  while (*names && *values)
    {
      if (strcmp (*names, "name") == 0)
        {
          lang = *values;
        }
      else if (strcmp (*names, "iso_639_2B_code") == 0 && code == NULL)
        {
          /* 2-letter ISO 639-1 codes have priority.
           * But some languages have no 2-letter code.
           * Ex: Asturian (ast).
           */
          code = *values;
        }
      else if (strcmp (*names, "iso_639_2T_code") == 0 && code == NULL)
        {
          code = *values;
        }
      else if (strcmp (*names, "iso_639_1_code") == 0)
        {
          code = *values;
        }

      names++;
      values++;
    }

  /* This is a hack for some special exception.
   * It seems localization won't work for the base language "zh". Probably because
   * written locale dialect are too different. So we have to be accurate and localize
   * separately each Chinese dialect we support.
   *
   * There was unfortunately no cleaner way to achieve this since there is no standardized
   * link between regions in iso-3166 and base languages in iso-639, which would allow
   * automatization for generating locale dialects codes.
   */
  if (g_strcmp0 (code, "zh") == 0)
    {
      gimp_language_store_self_l10n (parser->store, "Chinese", "zh_CN");
      gimp_language_store_self_l10n (parser->store, "Chinese", "zh_TW");
      gimp_language_store_self_l10n (parser->store, "Chinese", "zh_HK");
    }
  else
    gimp_language_store_self_l10n (parser->store, lang, code);
}

/* If possible, we want to localize a language in itself.
 * If it fails, fallback to the currently selected language, then to system lang.
 * Only fallback to C (en_US) as a last resort.
 */
static void
gimp_language_store_self_l10n (GimpLanguageStore *store,
                               const gchar       *lang,
                               const gchar       *code)
{
  if (lang && *lang && code && *code)
    {
      const gchar *semicolon;

      /* English does not need localization. */
      if (g_strcmp0 (code, "en") != 0)
        {
          gchar *current_lang = g_strdup (g_getenv ("LANGUAGE"));
          gchar *temp_lang;

          if (current_lang)
            temp_lang = g_strdup_printf ("%s:%s:%s", code, current_lang, setlocale (LC_ALL, NULL));
          else
            temp_lang = g_strdup (code);

          /* Temporarily change the localization language. */
          g_setenv ("LANGUAGE", temp_lang, TRUE);
          setlocale (LC_ALL, "");
          lang = dgettext ("iso_639", lang);
          if (current_lang)
            g_setenv ("LANGUAGE", current_lang, TRUE);
          else
            g_unsetenv("LANGUAGE");
          setlocale (LC_ALL, "");

          g_free (current_lang);
          g_free (temp_lang);
        }

      /*  there might be several language names; use the first one  */
      semicolon = strchr (lang, ';');

      if (semicolon)
        {
          gchar *first = g_strndup (lang, semicolon - lang);

          gimp_language_store_add (store, first, code);
          g_free (first);
        }
      else
        {
          gimp_language_store_add (store, lang, code);
        }
    }
}

static void
iso_codes_parser_start_element (GMarkupParseContext  *context,
                                const gchar          *element_name,
                                const gchar         **attribute_names,
                                const gchar         **attribute_values,
                                gpointer              user_data,
                                GError              **error)
{
  IsoCodesParser *parser = user_data;

  switch (parser->state)
    {
    case ISO_CODES_START:
      if (strcmp (element_name, "iso_639_entries") == 0)
        {
          parser->state = ISO_CODES_IN_ENTRIES;
          break;
        }

    case ISO_CODES_IN_ENTRIES:
      if (strcmp (element_name, "iso_639_entry") == 0)
        {
          parser->state = ISO_CODES_IN_ENTRY;
          iso_codes_parser_entry (parser, attribute_names, attribute_values);
          break;
        }

    case ISO_CODES_IN_ENTRY:
    case ISO_CODES_IN_UNKNOWN:
      iso_codes_parser_start_unknown (parser);
      break;
    }
}

static void
iso_codes_parser_end_element (GMarkupParseContext *context,
                              const gchar         *element_name,
                              gpointer             user_data,
                              GError             **error)
{
  IsoCodesParser *parser = user_data;

  switch (parser->state)
    {
    case ISO_CODES_START:
      g_warning ("%s: shouldn't get here", G_STRLOC);
      break;

    case ISO_CODES_IN_ENTRIES:
      parser->state = ISO_CODES_START;
      break;

    case ISO_CODES_IN_ENTRY:
      parser->state = ISO_CODES_IN_ENTRIES;
      break;

    case ISO_CODES_IN_UNKNOWN:
      iso_codes_parser_end_unknown (parser);
      break;
    }
}

static void
iso_codes_parser_start_unknown (IsoCodesParser *parser)
{
  if (parser->unknown_depth == 0)
    parser->last_known_state = parser->state;

  parser->state = ISO_CODES_IN_UNKNOWN;
  parser->unknown_depth++;
}

static void
iso_codes_parser_end_unknown (IsoCodesParser *parser)
{
  g_assert (parser->unknown_depth > 0 && parser->state == ISO_CODES_IN_UNKNOWN);

  parser->unknown_depth--;

  if (parser->unknown_depth == 0)
    parser->state = parser->last_known_state;
}
