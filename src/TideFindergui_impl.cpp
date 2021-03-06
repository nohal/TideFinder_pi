/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  TideFinder Plugin
 * Author:   Mike Rossiter
 *
 ***************************************************************************
 *   Copyright (C) 2013 by Mike Rossiter                                   *
 *   $EMAIL$                                                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************
 */

#include "TideFindergui_impl.h"
#include <wx/progdlg.h>
#include <wx/wx.h>
#include "wx/dir.h"
#include <list>
#include <cmath>
#include "TideFinder_pi.h"
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include "wx/dialog.h"
#include <wx/datetime.h>
#include "TCWin.h"
#include "wx/string.h"
#include <list>
#include <vector>
#include "timectrl.h"
#include "tcmgr.h"

class Position;
class TideFinder_pi;
class VMHData;
class Harmonics;
class TidalFactors;
class PortTides;
class TCWin;

extern wxArrayString    TideCurrentDataSet;

using namespace std;
// convert degrees to radians  
static inline double DegToRad(double deg) { return (deg * M_PI) / 180.0; }
// event handlers


/* WARNING:  These values are very important, as used under the "default" case. */
#define INT_PART 3
#define DEC_PART 2

static wxString port_clicked;

enum
{
            FORWARD_ONE_HOUR_STEP    =3600,
            FORWARD_TEN_MINUTES_STEP =600,
            FORWARD_ONE_MINUTES_STEP =60,
            BACKWARD_ONE_HOUR_STEP    =-3600,
            BACKWARD_TEN_MINUTES_STEP =-600,
            BACKWARD_ONE_MINUTES_STEP =-60
};

double Str2LatLong(char* coord){

    int sign = +1;
    double val;

    int i = 0;  /* an index into coord, the text-input string, indicating the character currently being parsed */

    int p[9] = {0,0,1,  /* degrees */
                0,0,1,  /* minutes */
                0,0,1   /* seconds */
               };
    int* ptr = p;   /* p starts at Degrees. 
                       It will advance to the Decimal part when a decimal-point is encountered,
                       and advance to minutes & seconds when a separator is encountered */
    int  flag = INT_PART; /* Flips back and forth from INT_PART and DEC_PART */

    while(1)
    {
        switch (coord[i])
        {
            /* Any digit contributes to either degrees,minutes, or seconds */
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                *ptr = 10* (*ptr) + (coord[i] - '0');
                if (flag == DEC_PART)  /* it'd be nice if I could find a clever way to avoid this test */
                {
                    ptr[1] *= 10;
                }
                break;

            case '.':     /* A decimal point implies ptr is on an integer-part; advance to decimal part */
                flag = DEC_PART; /* after encountering a decimal point, we are now processing the Decimal Part */
                ptr++;  /* ptr[0] is now the Decimal piece; ptr[1] is the Denominator piece (powers of 10) */
                break;

            /* A Null terminator triggers return (no break necessary) */
            case '\0':
                val = p[0]*3600 + p[3]*60 + p[6];             /* All Integer math */
                if (p[1]) val += ((double)p[1]/p[2]) * 3600;  /* Floating-point operations only if needed */
                if (p[4]) val += ((double)p[4]/p[5]) *   60;  /* (ditto) */
                if (p[7]) val += ((double)p[7]/p[8]);         /* (ditto) */
                return sign * val / 3600.0;                 /* Only one floating-point division! */

            case 'W':
            case 'S':
                sign = -1;
                break;

            /* Any other symbol is a separator, and moves ptr from degrees to minutes, or minutes to seconds */
            default:
                /* Note, by setting DEC_PART=2 and INT_PART=3, I avoid an if-test. (testing and branching is slow) */
                ptr += flag;
                flag = INT_PART; /* reset to Integer part, we're now starting a new "piece" (degrees, min, or sec). */
        }
        i++;
    }

    return -1.0;  /* Should never reach here! */
}



CfgDlg::CfgDlg( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : CfgDlgDef( parent, id, title, pos, size, style )
{
	
}

Dlg::Dlg( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : DlgDef( parent, id, title, pos, size, style )
{	
    this->Fit();
	LoadHarmonics();
	btc_valid = true;
    dbg=false; //for debug output set to true
}

void Dlg::OnClose(wxCloseEvent& event)
{
	wxString to_delete = _T("T") + m_PortNo;
	DeleteSingleWaypoint( to_delete );
	plugin->OnTideFinderDialogClose();
}

void Dlg::OnContextMenu(double m_lat, double m_lon){

	

	GetPortDialog aboutDialog ( this, -1, _("Select your Port"),
	                          wxPoint(200, 200), wxSize(300, 200) );

    
	aboutDialog.dialogText->InsertColumn(0, _T(""), 0 , wxLIST_AUTOSIZE);
	aboutDialog.dialogText->SetColumnWidth(0, 290);
	aboutDialog.dialogText->InsertColumn(1, _T(""), 0 , wxLIST_AUTOSIZE);
	aboutDialog.dialogText->SetColumnWidth(1, 0);
	aboutDialog.dialogText->DeleteAllItems();

	m_staticText2->SetLabel(wxT(""));

    bool foundPort = false;
	double radius = 0.1;
	double dist = 0;
	char N = 'N';
	int c = 0;
	wxString dimensions = wxT(""), s;
	wxString exPort = wxT("");
		
	wxListItem     row_info;  
	wxString       cell_contents_string = wxEmptyString;

	double lat = 50;
	double lon = -4;

	bool newItem = false;

	int i;
	
	while (!foundPort){
	        for ( i=1 ; i<ptcmgr->Get_max_IDX() +1 ; i++ )
            {				
						IDX_entry *pIDX = ptcmgr->GetIDX_entry (i);

                        char type = pIDX->IDX_type;             // Entry "TCtcIUu" identifier
                        if ( ( type == 't' ) ||  ( type == 'T' ) )  // only Tides
                        {                              

								lat = pIDX->IDX_lat;
								lon = pIDX->IDX_lon;

				 				dist = distance(lat,lon,m_lat,m_lon, N);
				     			if (dist < radius){
									wxString locn( pIDX->IDX_station_name, wxConvUTF8 );
									wxString locna, locnb;
									if( locn.Contains( wxString( _T ( "," ) ) ) ) {
										locna = locn.BeforeFirst( ',' );
										locnb = locn.AfterFirst( ',' );
									} else {
										locna = locn;
										locnb.Empty();
									}
									m_PortName = locna;
								    intPortNo = pIDX->IDX_rec_num ; 
								    m_PortNo = wxString::Format(wxT("%i"),intPortNo);
								    newItem = true;								  
							    }							  
						}											
							
				bool inList = false;

				int g = aboutDialog.dialogText->GetItemCount();
			    int p = 0; 
				for (p;p<g; p++){
 
							   // Set what row it is (m_itemId is a member of the regular wxListCtrl class)
							   row_info.m_itemId = p;
							   // Set what column of that row we want to query for information.
							   row_info.m_col = 1;
							   // Set text mask
							   row_info.m_mask = wxLIST_MASK_TEXT;
							   // Get the info and store it in row_info variable.   
							   aboutDialog.dialogText->GetItem( row_info );
							   // Extract the text out that cell
							   cell_contents_string = row_info.m_text; 
							
							   if (cell_contents_string == m_PortNo){								 
								  inList = true;								  
							   }						
				    }

						 if (!inList && newItem){
							wxListItem myItem;
							
							aboutDialog.dialogText->InsertItem(c,myItem);
							aboutDialog.dialogText->SetItem(c,0,m_PortName);
							aboutDialog.dialogText->SetItem(c,1,m_PortNo);
							wxString myGUID = _T("T") + m_PortNo;
							myWP = new PlugIn_Waypoint(lat,lon,_T("circle"),m_PortName,myGUID);
							AddSingleWaypoint(myWP,false);
							
							newItem = false;

							c++;
							if (c == 4){
									foundPort = true;
									break;
							}	
						}
					  
					     
			}
        
		radius = radius + 5;
		i = 1;
	 }
	 
	this->m_parent->Refresh();

	long si = -1;
	long itemIndex = -1;     
	int f = 0;

	 if ( aboutDialog.ShowModal() != wxID_OK ){
		 for (f;f<4;f++) {
         itemIndex = aboutDialog.dialogText->GetNextItem(itemIndex,
                                         wxLIST_NEXT_ALL,
                                         wxLIST_STATE_SELECTED);
		 si = f;
		 row_info.m_itemId = si;
			   // Set what column of that row we want to query for information.
			   row_info.m_col = 1;
			   // Set text mask
			   row_info.m_mask = wxLIST_MASK_TEXT;
			   // Get the info and store it in row_info variable.   
			   aboutDialog.dialogText->GetItem( row_info );
			   // Extract the text out that cell
			   m_PortNo = row_info.m_text;			  
			    // Delete the waypoint
			   wxString t = _T("T") + m_PortNo;   
			   DeleteSingleWaypoint(t);

			   CAL_button->Hide();
			   PR_button->Hide();
			   NX_button->Hide();
			   GF_button->Hide();
			   m_listBox1->Hide();
			   m_staticText3->Hide();
			   m_staticText2->SetLabel( wxT("Use Right-Click in the location for tide prediction") );
			   this->Refresh();
			   this->Fit();
		 }
	 }
	 else{
		 m_listBox1->Show();
		 m_staticText3->Show();
		 CAL_button->Show();
		 PR_button->Show();
	     NX_button->Show();
	     GF_button->Show();
		 
		 foundPort = false;
		 
		 for (f;f<4;f++) {
			   itemIndex = aboutDialog.dialogText->GetNextItem(itemIndex,
											 wxLIST_NEXT_ALL,
											 wxLIST_STATE_SELECTED);
			   si = f;

			   row_info.m_itemId = si;
			   // Set what column of that row we want to query for information.
			   row_info.m_col = 1;
			   // Set text mask
			   row_info.m_mask = wxLIST_MASK_TEXT;
 
			   // Get the info and store it in row_info variable.   
			   aboutDialog.dialogText->GetItem( row_info );
			   // Extract the text out that cell
			   m_PortNo = row_info.m_text;
			    // Delete the waypoint
			   wxString t = _T("T") + m_PortNo;
			   SelectedPorts[f] = t;			   
		 }

         for (;;) {
         itemIndex = aboutDialog.dialogText->GetNextItem(itemIndex,
                                         wxLIST_NEXT_ALL,
                                         wxLIST_STATE_SELECTED);
 
         if (itemIndex == -1) break;
		
		 // Got the selected item index
		  if (aboutDialog.dialogText->IsSelected(itemIndex)){
			  si = itemIndex;
			  foundPort = true;
			  break;			  					 		
		   } 
		 }
		   if (foundPort){
			   // Set what row it is (m_itemId is a member of the regular wxListCtrl class)
			   row_info.m_itemId = si;
			   // Set what column of that row we want to query for information.
			   row_info.m_col = 1;
			   // Set text mask
			   row_info.m_mask = wxLIST_MASK_TEXT;
 
			   // Get the info and store it in row_info variable.   
			   aboutDialog.dialogText->GetItem( row_info );		  
			   // Extract the text out that cell
			   m_PortNo = row_info.m_text;
			   // Delete the other waypoints
			   f = 0;
			   for (f;f<4;f++) {
					wxString sp = SelectedPorts[f];
			      if (_T("T") + m_PortNo != sp) 
			         DeleteSingleWaypoint(sp);
			   }
               intPortNo = wxAtoi(m_PortNo);
			   row_info.m_col = 0;

			   aboutDialog.dialogText->GetItem( row_info );
			   m_PortName = row_info.m_text;
			   m_staticText2->SetLabel(m_PortName);
			   btc_valid = true;  // Start with today's tides
               OnCalculate();
			   this->Refresh();
			   this->Fit();
		  }
		  else{
			   this->Refresh();
			   this->Fit();
               wxMessageBox(wxT("No Port Selected"), _T("No Selection"));
		   }

	}
}


#define pi 3.14159265358979323846
double Dlg::distance(double lat1, double lon1, double lat2, double lon2, char unit) {
 double theta, dist;
 theta = lon1 - lon2;
 dist = sin(deg2rad(lat1)) * sin(deg2rad(lat2)) + cos(deg2rad(lat1)) * cos(deg2rad(lat2)) * cos(deg2rad(theta));
 dist = acos(dist);
 dist = rad2deg(dist);
 dist = dist * 60 * 1.1515;
 switch(unit) {
case 'M':
 break;
case 'K':
 dist = dist * 1.609344;
 break;
case 'N':
 dist = dist * 0.8684;
 break;
 }
 return (dist);
}
/*:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::*/
/*:: This function converts decimal degrees to radians :*/
/*:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::*/
double Dlg::deg2rad(double deg) {
 return (deg * pi / 180);
}
/*:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::*/
/*:: This function converts radians to decimal degrees :*/
/*:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::*/
double Dlg::rad2deg(double rad) {
 return (rad * 180 / pi);
}

void Dlg::LoadHarmonics()
{
	  //  Establish a "home" location
        
	  g_SData_Locn = *GetpSharedDataLocation();

      // Establish location of Tide and Current data
      pTC_Dir = new wxString(_T("tcdata"));
      pTC_Dir->Prepend(g_SData_Locn);
      pTC_Dir->Append(wxFileName::GetPathSeparator());  
	
      wxString TCDir;
      TCDir = *pTC_Dir;
      
      wxLogMessage(_T("Using Tide/Current data from:  ") + TCDir);
	  wxString cache_locn = TCDir; 

	  wxString harm2test = TCDir;
      harm2test.Append( _T("HARMONIC") );
	
	  ptcmgr = new TCMgr(TCDir, cache_locn);     	
}

void Dlg::OnCalculate(){
	
	//    Figure out this computer timezone minute offset
    wxDateTime this_now = wxDateTime::Now();
    wxDateTime this_gmt = this_now.ToGMT();
	 
#if wxCHECK_VERSION(2, 6, 2)
    wxTimeSpan diff = this_now.Subtract( this_gmt );
#else
    wxTimeSpan diff = this_gmt.Subtract ( this_now );
#endif

    int diff_mins = diff.GetMinutes();

    int station_offset = 0;

    m_corr_mins = station_offset - diff_mins;
    if( this_now.IsDST() ) m_corr_mins += 60;
	
if (btc_valid){

//    Establish the inital drawing day as today
	m_graphday = wxDateTime::Now();
    wxDateTime graphday_00 = wxDateTime::Today();
    time_t t_graphday_00 = graphday_00.GetTicks();

    //    Correct a Bug in wxWidgets time support
    if( !graphday_00.IsDST() && m_graphday.IsDST() ) t_graphday_00 -= 3600;
    if( graphday_00.IsDST() && !m_graphday.IsDST() ) t_graphday_00 += 3600;

    m_t_graphday_00_at_station = t_graphday_00 - ( m_corr_mins * 60 );

	}

	int d = m_graphday.GetDayOfYear();
	wxString MyDay = wxString::Format(wxT("%d"),(int) d); // For harmonics Table 7

	wxString s_UTC;
	if (m_graphday.IsDST()){
		s_UTC = wxT(" (Z+1)");
	}
	else{
		s_UTC = wxT(" (Z)");
	}

	wxString s0 = m_graphday.Format( _T ("%A %d %B %Y"));
	wxString s1 = m_graphday.Format(_T("%H:%M"));			 
	s2time = s0 + _(" ") + s1 + s_UTC;			
						
	m_staticText3->SetLabel(s2time);  
  	myTime = m_graphday.GetTicks();

	CalcHWLW(intPortNo);  //port_clicked); 	
}


void Dlg::CalcHWLW(int PortCode)
{
	    m_listBox1->Clear();

		m_PortNo = wxString::Format(wxT("%i"),PortCode);

		pIDX = ptcmgr->GetIDX_entry ( PortCode );
		float dir;
		
		int i, c, n, e;
		c = 0;
		e = 0;
		double myArrayOfRanges[8];

		float tcmax, tcmin;
		//float dir;
                        
		tcmax = -10;
        tcmin = 10;
        
		float val = 0;
		int list_index = 0 ;
		int array_index = 0;
        bool  wt = 0;
		float myLW, myHW;

		Station_Data *pmsd;
		wxString sHWLW = _T("");
		

                        // get tide flow sens ( flood or ebb ? )
						
                        ptcmgr->GetTideFlowSens(m_t_graphday_00_at_station, BACKWARD_ONE_HOUR_STEP, pIDX->IDX_rec_num, tcv[0], val, wt);
		
						for ( i=0 ; i<26 ; i++ )
                        {
                                int tt = m_t_graphday_00_at_station + ( i * FORWARD_ONE_HOUR_STEP );
                                ptcmgr->GetTideOrCurrent ( tt, pIDX->IDX_rec_num, tcv[i], dir );
								
                                if ( tcv[i] > tcmax )
                                        tcmax = tcv[i];

                                                if ( tcv[i] < tcmin )
                                                   tcmin = tcv[i];                                                
                                                    if ( ! ((tcv[i] > val) == wt) )                // if tide flow sens change
                                                    {
                                                      float tcvalue;                                        //look backward for HW or LW
                                                      time_t tctime;
                                                      ptcmgr->GetHightOrLowTide(tt, BACKWARD_TEN_MINUTES_STEP, BACKWARD_ONE_MINUTES_STEP, tcv[i], wt, pIDX->IDX_rec_num, tcvalue, tctime);

                                                      wxDateTime tcd ;                                                              //write date
                                                      wxString s, s1, s2;
                                                      tcd.Set( tctime + ( m_corr_mins * 60 ) ) ;

													  s2 = tcd.Format ( _T ("%A %d %B %Y"));
                                                      s.Printf(tcd.Format(_T("%H:%M  ")));													 

                                                      s1.Printf( _T("%05.2f "),tcvalue);    												  
	
													  pmsd = pIDX->pref_sta_data;                         //write unit 													  
													  
													  ( wt )? sHWLW = _("HW") : sHWLW = _("LW"); 
																                                                        
													  // Fill the array with tide data
													  TC[array_index][0] = s2 + _(" ") + s;													  													
													  TC[array_index][1] = s1;
													  TC[array_index][2] = wxString(pmsd->units_abbrv ,wxConvUTF8);
													  TC[array_index][3] = sHWLW;

													  if (TC[array_index][3] == _("LW")) 
													  {									
														myLW = tcvalue;
														wxString s_LW = wxString::Format(wxT("%05.2f"),(float) myLW);
														m_listBox1->Insert((TC[array_index][0]) + _T("  ") + _T("LW: ") + s_LW + _T(" ") + TC[array_index][2] ,list_index);
														myUnits = TC[array_index][2];
														list_index++;
													  }
													  
													  if (TC[array_index][3] == _("HW")) 
													  {
														myHW = tcvalue;
														wxString s_HW = wxString::Format(wxT("%05.2f"),(float) myHW);
														m_listBox1->Insert((TC[array_index][0]) + _T("  ") + _T("HW: ") + s_HW  + _T(" ") + TC[array_index][2] , list_index);
														list_index++;
													  }  

													   myRange = myHW - myLW;
													 
													  if ((abs(myRange) == myHW) || (abs(myRange) == myLW))
													  {
															// out of range
													  }
													  else
													  {
														  myArrayOfRanges[c] = myRange;
														  c++;
													  }
														
													  array_index++;
													  
                                                      wt = !wt ;     //change tide flow sens

                                                    }

													val = tcv[i];                                                                                                
                        }

						float tidalheight, risefall;
					    ptcmgr->GetTideOrCurrent(myTime, PortCode, tidalheight, risefall);

						wxString th;
						th = wxString::Format(wxT("%05.2f"),tidalheight);

						wxString ot;

					    myUnits = wxString(pmsd->units_abbrv ,wxConvUTF8);
						ot = s2time + _T("   Height: ") + th + _T(" ") + myUnits;
		
						m_staticText3->SetLabel(ot);


						c--;
						n = 0;
						double AddRanges = 0;
						for (n; n<c; n++){
						   AddRanges = AddRanges + myArrayOfRanges[n];
						}
						// myRange for the speed of current calculation
						myRange = AddRanges/n;												
}


int Dlg::FindPortID(wxString myPort)
{	int t;
	        for ( int i=1 ; i<ptcmgr->Get_max_IDX() +1 ; i++ )
            {				
						IDX_entry *pIDX = ptcmgr->GetIDX_entry (i);

                        char type = pIDX->IDX_type;             // Entry "TCtcIUu" identifier
                        if ( ( type == 't' ) ||  ( type == 'T' ) )  // only Tides
                        {                              
							  wxString s = wxString(pIDX->IDX_reference_name,wxConvUTF8); 
							  if ( s == myPort)
							  {		
								  return i;
							  }							  
						}
						t = i;
			}
			return 0;
}

void Dlg::NXEvent( wxCommandEvent& event )
{	
	wxTimeSpan dt( 24, 0, 0, 0 );
    m_graphday.Add( dt );
    wxDateTime dm = m_graphday;

    wxDateTime graphday_00 = dm.ResetTime();
    if(graphday_00.GetYear() == 2013)
        int yyp = 4;

    time_t t_graphday_00 = graphday_00.GetTicks();
    if( !graphday_00.IsDST() && m_graphday.IsDST() ) t_graphday_00 -= 3600;
    if( graphday_00.IsDST() && !m_graphday.IsDST() ) t_graphday_00 += 3600;
    m_t_graphday_00_at_station = t_graphday_00 - ( m_corr_mins * 60 );

    btc_valid = false;
	CalcHWLW(intPortNo);
}

void Dlg::PREvent( wxCommandEvent& event )
{
    wxTimeSpan dt( -24, 0, 0, 0 );
    m_graphday.Add( dt );
    wxDateTime dm = m_graphday;

    wxDateTime graphday_00 = dm.ResetTime();
    time_t t_graphday_00 = graphday_00.GetTicks();

    if( !graphday_00.IsDST() && m_graphday.IsDST() ) t_graphday_00 -= 3600;
    if( graphday_00.IsDST() && !m_graphday.IsDST() ) t_graphday_00 += 3600;

    m_t_graphday_00_at_station = t_graphday_00 - ( m_corr_mins * 60 );

    btc_valid = false;
    CalcHWLW(intPortNo);
}

void Dlg::OnCalendarShow( wxCommandEvent& event )
{	

	CalendarDialog CalDialog ( this, -1, _("Select the date for Tides"),
	                          wxPoint(100, 100), wxSize(200, 250) );
	if ( CalDialog.ShowModal() == wxID_OK ){
				
		wxString myTime = CalDialog._timeText->GetValue();
        wxString val = myTime.Mid(0,1);

		if ( val == wxT(" ")){
			myTime = wxT("0") + myTime.Mid(1,5);
		}

		wxDateTime dt;
		dt.ParseTime(myTime);
		
		wxString todayHours = dt.Format(_T("%H"));
		wxString todayMinutes = dt.Format(_T("%M"));
	
		double h;
		double m;

		todayHours.ToDouble(&h);
		todayMinutes.ToDouble(&m);
		myTimeOfDay = wxTimeSpan(h,m,0,0);	

		wxDateTime dm = CalDialog.dialogCalendar->GetDate();
		wxDateTime yn = wxDateTime::Now();
		int mdm = dm.GetYear();
		int myn = yn.GetYear();
		if(mdm != myn){
		wxMessageBox(wxT("Sorry, only the current year will work!"),wxT("Out of current year"));
		dm = yn;
		}
		

		m_graphday = dm + myTimeOfDay;

		wxDateTime graphday_00 = dm.ResetTime();

		if(graphday_00.GetYear() == 2013)
			int yyp = 4;

		time_t t_graphday_00 = graphday_00.GetTicks();
		if( !graphday_00.IsDST() && m_graphday.IsDST() ) t_graphday_00 -= 3600;
		if( graphday_00.IsDST() && !m_graphday.IsDST() ) t_graphday_00 += 3600;
		m_t_graphday_00_at_station = t_graphday_00 - ( m_corr_mins * 60 );
		
		btc_valid = false;
		CalcHWLW(intPortNo);
	}	
}

void Dlg::GFEvent(wxCommandEvent& event){

	TCWin *myTCWin = new TCWin(this,100,100, intPortNo, m_PortName, m_t_graphday_00_at_station, m_graphday, myUnits);
	myTCWin->Show();

}

void Dlg::CalcMyTimeOfDay(){

	wxDateTime this_now = wxDateTime::Now();

	wxString todayHours = this_now.Format(_T("%H"));
	wxString todayMinutes = this_now.Format(_T("%M"));
	
	double h;
	double m;

	todayHours.ToDouble(&h);
	todayMinutes.ToDouble(&m);
	wxTimeSpan myTimeOfDay = wxTimeSpan(h,m,0,0);	
}

CalendarDialog::CalendarDialog ( wxWindow * parent, wxWindowID id, const wxString & title,
                           const wxPoint & position, const wxSize & size, long style )
: wxDialog( parent, id, title, position, size, style)
{
		
	wxString dimensions = wxT(""), s;
	wxPoint p;
	wxSize  sz;
 
	sz.SetWidth(180);
	sz.SetHeight(150);
	
	p.x = 6; p.y = 2;
	s.Printf(_(" x = %d y = %d\n"), p.x, p.y);
	dimensions.append(s);
	s.Printf(_(" width = %d height = %d\n"), sz.GetWidth(), sz.GetHeight());
	dimensions.append(s);
	dimensions.append(wxT("here"));
 
    dialogCalendar = new wxCalendarCtrl(this, -1, wxDefaultDateTime, p, sz, wxCAL_SHOW_HOLIDAYS ,wxT("Tide Calendar"));
	
	wxWindowID text, spinner;

	m_staticText = new wxStaticText(this,text,wxT("Time:"),wxPoint(15,155),wxSize(60,21));

	_timeText = new wxTimeTextCtrl(this,text,wxT("12:00"),wxPoint(75,155),wxSize(60,21));

    _spinCtrl=new wxSpinButton(this,spinner,wxPoint(136,155),wxSize(20,21),wxSP_VERTICAL|wxSP_ARROW_KEYS);
	_spinCtrl->Connect( wxEVT_SCROLL_LINEUP, wxSpinEventHandler( CalendarDialog::spinUp ), NULL, this );
	_spinCtrl->Connect( wxEVT_SCROLL_LINEDOWN, wxSpinEventHandler( CalendarDialog::spinDown ), NULL, this );
	
	p.y += sz.GetHeight() + 30;
	wxButton * b = new wxButton( this, wxID_OK, _("OK"), p, wxDefaultSize );
	p.x += 110;
	wxButton * c = new wxButton( this, wxID_CANCEL, _("Cancel"), p, wxDefaultSize );
    
}



void CalendarDialog::spinUp(wxSpinEvent& event)
{
		_timeText->OnArrowUp();
}

void CalendarDialog::spinDown(wxSpinEvent& event)
{
         _timeText->OnArrowDown();
}
	

GetPortDialog::GetPortDialog ( wxWindow * parent, wxWindowID id, const wxString & title,
                           const wxPoint & position, const wxSize & size, long style )
: wxDialog( parent, id, title, position, size, style)
{
	
	wxString dimensions = wxT(""), s;
	wxPoint p;
	wxSize  sz;
 
	sz.SetWidth(size.GetWidth() - 20);
	sz.SetHeight(size.GetHeight() - 70);
 
	p.x = 6; p.y = 2;
 
	dialogText = new wxListView(this, wxID_ANY, p, sz, wxLC_NO_HEADER|wxLC_REPORT|wxLC_SINGLE_SEL, wxDefaultValidator, wxT(""));

    wxFont *pVLFont = wxTheFontList->FindOrCreateFont( 12, wxFONTFAMILY_SWISS, wxNORMAL, wxFONTWEIGHT_NORMAL ,
                                                      FALSE, wxString( _T ( "Arial" ) ) );
	dialogText->SetFont(*pVLFont);

	p.y += sz.GetHeight() + 10;
	wxButton * b = new wxButton( this, wxID_OK, _("OK"), p, wxDefaultSize );
	p.x += 110;
	wxButton * c = new wxButton( this, wxID_CANCEL, _("Cancel"), p, wxDefaultSize );
};

