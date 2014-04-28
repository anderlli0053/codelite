//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//
// copyright            : (C) 2008 by Eran Ifrah
// file name            : environmentconfig.cpp
//
// -------------------------------------------------------------------------
// A
//              _____           _      _     _ _
//             /  __ \         | |    | |   (_) |
//             | /  \/ ___   __| | ___| |    _| |_ ___
//             | |    / _ \ / _  |/ _ \ |   | | __/ _ )
//             | \__/\ (_) | (_| |  __/ |___| | ||  __/
//              \____/\___/ \__,_|\___\_____/_|\__\___|
//
//                                                  F i l e
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
#include "wx/utils.h"
#include "xmlutils.h"
#include "wx/regex.h"
#include "wx/filename.h"
#include "environmentconfig.h"
#include "evnvarlist.h"
#include "wx_xml_compatibility.h"
#include <wx/log.h>
#include "macromanager.h"

const wxString __NO_SUCH_ENV__ = wxT("__NO_SUCH_ENV__");

static EnvironmentConfig* ms_instance = NULL;

//------------------------------------------------------------------------------

EnvironmentConfig::EnvironmentConfig()
    : m_envApplied(0)
{
}

EnvironmentConfig::~EnvironmentConfig()
{
}

EnvironmentConfig* EnvironmentConfig::Instance()
{
    if (ms_instance == 0) {
        ms_instance = new EnvironmentConfig();
    }
    return ms_instance;
}

void EnvironmentConfig::Release()
{
    if (ms_instance) {
        delete ms_instance;
    }
    ms_instance = 0;
}

wxString EnvironmentConfig::GetRootName()
{
    return wxT("EnvironmentVariables");
}

bool EnvironmentConfig::Load()
{
    bool loaded = ConfigurationToolBase::Load( wxT("config/environment_variables.xml") );
    if(loaded) {
        // make sure that we are using the new format
        wxXmlNode *node = XmlUtils::FindFirstByTagName(m_doc.GetRoot(), wxT("ArchiveObject"));
        if(node) {
            node = XmlUtils::FindFirstByTagName(node, wxT("StringMap"));
            if(node) {

                // this is an old version, convert it to the new format
                EvnVarList vars;
                std::map<wxString, wxString> envSets;
                wxString content;

                wxXmlNode *child = node->GetChildren();
                while(child) {
                    if(child->GetName() == wxT("MapEntry")) {
                        wxString key = child->GetPropVal(wxT("Key"),   wxT(""));
                        wxString val = child->GetPropVal(wxT("Value"), wxT(""));
                        content << key << wxT("=") << val << wxT("\n");
                    }
                    child = child->GetNext();
                }
                envSets[wxT("Default")] = content.Trim().Trim(false);
                vars.SetEnvVarSets(envSets);
                SetSettings(vars);
            }
        }
    }
    return loaded;
}

wxString EnvironmentConfig::ExpandVariables(const wxString &in, bool applyEnvironment)
{
    EnvSetter *env = NULL;
    if(applyEnvironment) {
        env = new EnvSetter(this);
    }

    wxString expandedValue = DoExpandVariables(in);

    delete env;
    return expandedValue;
}

void EnvironmentConfig::ApplyEnv(wxStringMap_t *overrideMap, const wxString &project)
{
    // Dont allow recursive apply of the environment
    m_envApplied++;

    if(m_envApplied > 1)
        return;

    //read the environments variables
    EvnVarList vars;
    ReadObject(wxT("Variables"), &vars);

    // get the active environment variables set
    EnvMap variables = vars.GetVariables(wxEmptyString, true, project);

    // if we have an "override map" place all the entries from the override map
    // into the global map before applying the environment
    if(overrideMap) {
        wxStringMap_t::iterator it = overrideMap->begin();
        for(; it != overrideMap->end(); it++) {
            variables.Put(it->first, it->second);
        }
    }

    m_envSnapshot.clear();
    for (size_t i=0; i<variables.GetCount(); i++) {

        wxString key, val;
        variables.Get(i, key, val);

        //keep old value before changing it
        wxString oldVal(wxEmptyString);
        if( wxGetEnv(key, &oldVal) == false ) {
            oldVal = __NO_SUCH_ENV__;
        }

        // keep the old value, however, don't override it if it
        // already exists as it might cause the variable to grow in size...
        // Simple case:
        // PATH=$(PATH);\New\Path
        // PATH=$(PATH);\Another\New\Path
        // If we replace the value, PATH will contain the original PATH + \New\Path
        if ( m_envSnapshot.count( key ) == 0 ) {
            m_envSnapshot.insert( std::make_pair( key, oldVal ) );
        }

        // Incase this line contains other environment variables, expand them before setting this environment variable
        wxString newVal = DoExpandVariables(val);

        //set the new value
        wxSetEnv(key, newVal);
    }
}

void EnvironmentConfig::UnApplyEnv()
{
    m_envApplied--;

    if(m_envApplied == 0) {
        //loop over the old values and restore them
        wxStringMap_t::iterator iter = m_envSnapshot.begin();
        for ( ; iter != m_envSnapshot.end(); iter++ ) {
            wxString key = iter->first;
            wxString value = iter->second;
            if ( value == __NO_SUCH_ENV__ ) {
                // Remove the environment completely
                ::wxUnsetEnv(key);
            } else {
                // Restore old value
                ::wxSetEnv(key, value);
            }
        }
        m_envSnapshot.clear();
    }
}

EvnVarList EnvironmentConfig::GetSettings()
{
    EvnVarList vars;
    ReadObject(wxT("Variables"), &vars);
    return vars;
}

void EnvironmentConfig::SetSettings(EvnVarList &vars)
{
    WriteObject(wxT("Variables"), &vars);
}

wxString EnvironmentConfig::DoExpandVariables(const wxString& in)
{
    wxString result(in);
    wxString varName, text;

    DollarEscaper de(result);
    while ( MacroManager::Instance()->FindVariable(result, varName, text) ) {

        wxString replacement;
        if(varName == wxT("MAKE")) {
            //ignore this variable, since it is probably was passed here
            //by the makefile generator
            replacement = wxT("___MAKE___");

        } else {

            //search for an environment with this name
            wxGetEnv(varName, &replacement);
        }
        result.Replace(text, replacement);
    }

    //restore the ___MAKE___ back to $(MAKE)
    result.Replace(wxT("___MAKE___"), wxT("$(MAKE)"));
    return result;
}

wxArrayString EnvironmentConfig::GetActiveSetEnvNames(bool includeWorkspace, const wxString& project)
{
    //read the environments variables
    EvnVarList vars;
    ReadObject(wxT("Variables"), &vars);
    
    wxArrayString envnames;
    // get the active environment variables set
    EnvMap variables = vars.GetVariables(wxEmptyString, includeWorkspace, project);
    for(size_t i=0; i<variables.GetCount(); ++i) {
        wxString key, val;
        variables.Get(i, key, val);
        envnames.Add( key );
    }
    return envnames;
}

// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
// --------------------------------------------------------------------------

DollarEscaper::DollarEscaper(wxString& str)
    : m_str(str)
{
    m_str.Replace("$$", "@@ESC_DOLLAR@@");
}

DollarEscaper::~DollarEscaper()
{
    // we restore it to non escaped $ !!
    m_str.Replace("@@ESC_DOLLAR@@", "$");
}
