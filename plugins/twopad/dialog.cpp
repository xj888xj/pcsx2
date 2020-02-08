/*  TwoPAD - author: arcum42(@gmail.com)
 *  Copyright (C) 2019
 *
 *  Based on ZeroPAD, author zerofrog@gmail.com
 *  And OnePAD, author arcum42, gregory, PCSX2 team
 *  Copyright (C) 2006-2007
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "dialog.h"

configDialog *conf;

void initDialog()
{
    conf = new configDialog( nullptr, -1, _T("TwoPad"), wxDefaultPosition, wxSize(DEFAULT_WIDTH, DEFAULT_HEIGHT), wxSYSTEM_MENU | wxCAPTION | wxCLOSE_BOX | wxCLIP_CHILDREN);
}

void configDialog::addGamepad(padControls &pad, const wxString controllerName)
{
    wxArrayString controllers;
    controllers.Add(_T("None"));
    for (auto pad : sdl_pad)
    {
        controllers.Add(pad->name);
    }

    pad.Box = new wxStaticBoxSizer(wxVERTICAL, panel, controllerName);

    pad.Ctl = new wxChoice(
        panel, // Parent
        wxID_ANY,             // ID
        wxDefaultPosition/*wxPoint(20, 50)*/,      // Position
        wxDefaultSize,        // Size
        controllers);
    pad.Ctl->SetStringSelection(_T("None"));

    auto *rev = new wxBoxSizer(wxHORIZONTAL);
    pad.RevX = new wxCheckBox(panel, -1, "reverse LX");
    pad.RevY = new wxCheckBox(panel, -1, "reverse LY");
    rev->Add(pad.RevX);
    rev->Add(pad.RevY);

    pad.Rumble = new wxCheckBox(panel, -1, "Rumble");

    pad.Box->Add(pad.Ctl);
    pad.Box->Add(rev);
    pad.Box->Add(pad.Rumble);
}

configDialog::configDialog( wxWindow * parent, wxWindowID id, const wxString & title,
                           const wxPoint & position, const wxSize & size, long style )
: wxDialog( parent, id, title, position, size, style)
{
    panel = new wxPanel(this, -1);
    auto sizer = new wxBoxSizer(wxVERTICAL);

    addGamepad(pad1, _T("Controller 1 (nonfunctional)"));
    addGamepad(pad2, _T("Controller 2 (nonfunctional)"));
    
    sizer->Add(pad1.Box, 1, wxEXPAND | wxALL, 5);
    sizer->Add(
        new wxStaticLine(panel, -1),
        wxSizerFlags(1).Center().Expand().Border(wxALL, 3));
    sizer->Add(pad2.Box, 3, wxEXPAND | wxALL, 5);

    panel->SetSizerAndFit(sizer);
}

configDialog::~configDialog()
{
     Destroy();
}