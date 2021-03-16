// BWF MetaEdit Riff - RIFF stuff for BWF MetaEdit
//
// This code was created in 2010 for the Library of Congress and the
// other federal government agencies participating in the Federal Agencies
// Digital Guidelines Initiative and it is in the public domain.
//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//---------------------------------------------------------------------------
#include "Riff/Riff_Handler.h"
#include "Riff/Riff_Chunks.h"
#include "Common/Codes.h"
#include <sstream>
#include <iostream>
#include <algorithm>
#include "ZenLib/ZtringListList.h"
#include "ZenLib/File.h"
#include "ZenLib/Dir.h"
#include "TinyXml2/tinyxml2.h"

#ifdef MACSTORE
#include "Common/Mac_Helpers.h"
#endif

using namespace std;
using namespace ZenLib;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
const Ztring Riff_Handler_EmptyZtring_Const; //Use it when we can't return a reference to a true Ztring, const version
//---------------------------------------------------------------------------

//***************************************************************************
// Const
//***************************************************************************

enum xxxx_Fields
{
    Fields_Tech,
    Fields_Bext,
    Fields_Info,
    Fields_Adtl,
    Fields_Max,
};

size_t xxxx_Strings_Size[]=
{
    5,  //Tech
    15,  //Bext
    17, //Info
    3, //Adtl
};

const char* xxxx_Strings[][17]=
{
    {
        "XMP",
        "aXML",
        "iXML",
        "MD5Stored",
        "cue",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
    },
    {
        "Description",
        "Originator",
        "OriginatorReference",
        "OriginationDate",
        "OriginationTime",
        "TimeReference (translated)",
        "TimeReference",
        "BextVersion",
        "UMID",
        "LoudnessValue",
        "LoudnessRange",
        "MaxTruePeakLevel",
        "MaxMomentaryLoudness",
        "MaxShortTermLoudness",
        "CodingHistory",
        "",
        "",
    },
    {
        //Note: there is a duplicate in Riff_Chunks_INFO_xxxx
        "IARL", //Archival Location
        "IART", //Artist
        "ICMS", //Commissioned
        "ICMT", //Comment
        "ICOP", //Copyright
        "ICRD", //Date Created
        "IENG", //Engineer
        "IGNR", //Genre
        "IKEY", //Keywords
        "IMED", //Medium
        "INAM", //Title
        "IPRD", //Product
        "ISBJ", //Subject
        "ISFT", //Software
        "ISRC", //Source
        "ISRF", //Source Form
        "ITCH", //Technician
    },
    {
        "labl",
        "note",
        "ltxt",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
    },
};

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
Riff_Handler::Riff_Handler ()
{
    //Configuration
    riff2rf64_Reject=false;
    Overwrite_Reject=false;
    NoPadding_Accept=false;
    NewChunksAtTheEnd=false;
    GenerateMD5=false;
    VerifyMD5=false;
    VerifyMD5_Force=false;
    EmbedMD5=false;
    EmbedMD5_AuthorizeOverWritting=false;
    Bext_DefaultVersion=0;
    Bext_MaxVersion=2;

    //Internal
    Chunks=NULL;
    File_IsValid=false;
    File_IsCanceled=false;
}

//---------------------------------------------------------------------------
Riff_Handler::~Riff_Handler ()
{
    delete Chunks; //Chunks=NULL;
}

//***************************************************************************
// I/O
//***************************************************************************

//---------------------------------------------------------------------------
bool Riff_Handler::Open(const string &FileName)
{
    CriticalSectionLocker CSL(CS);

    return Open_Internal(FileName);
}

//---------------------------------------------------------------------------
bool Riff_Handler::Open_Internal(const string &FileName)
{
    //Init
    PerFile_Error.str(string());
    File_IsValid=false;
    File_IsCanceled=false;
    bool ReturnValue=true;
    
    //Global info
    delete Chunks; Chunks=new Riff();
    Chunks->Global->File_Name=Ztring().From_UTF8(FileName);

    //Opening file
    if (!File::Exists(Ztring().From_UTF8(FileName)) || !Chunks->Global->In.Open(Ztring().From_UTF8(FileName)))
    {
        Errors<<FileName<<": File does not exist"<<endl;
        PerFile_Error<<"File does not exist"<<endl;
        return false;
    }
    Chunks->Global->File_Size=Chunks->Global->In.Size_Get();
    Chunks->Global->File_Date=Chunks->Global->In.Created_Local_Get().To_UTF8();
    if (Chunks->Global->File_Date.empty())
        Chunks->Global->File_Date=Chunks->Global->In.Modified_Local_Get().To_UTF8();

    //Base
    Riff_Base::chunk Chunk;
    Chunk.Content.Size=Chunks->Global->File_Size;
    Options_Update_Internal(false);

    //Parsing
    try
    {
        Chunks->Read(Chunk);
        File_IsValid=true;
    }
    catch (exception_canceled &)
    {
        CriticalSectionLocker(Chunks->Global->CS);
        File_IsCanceled=true;
        Chunks->Global->Canceling=false;
        ReturnValue=false;
    }
    catch (exception_read_chunk &e)
    {
        Errors<<Chunks->Global->File_Name.To_UTF8()<<": "<<Ztring().From_CC4(Chunk.Header.Name).To_UTF8()<<" "<<e.what()<<endl;
        PerFile_Error<<Ztring().From_CC4(Chunk.Header.Name).To_UTF8()<<" "<<e.what()<<endl;
    }
    catch (exception &e)
    {
        Errors<<Chunks->Global->File_Name.To_UTF8()<<": "<<e.what()<<endl;
        PerFile_Error<<e.what()<<endl;
        ReturnValue=false;
    }

    //Cleanup
    Chunks->Global->In.Close();

    //ReadOnly check
    if (!File().Open(Ztring().From_UTF8(FileName), File::Access_Write))
    {
        Chunks->Global->Read_Only=true;
            Information<<Chunks->Global->File_Name.To_UTF8()<<": Is read only"<<endl;
            PerFile_Information<<"File is read only"<<endl;
    }

    if (File_IsValid)
    {
        //Log
        if (Chunks->Global->NoPadding_IsCorrected)
        {
            Information<<Chunks->Global->File_Name.To_UTF8()<<": no-padding correction"<<endl;
            PerFile_Information<<"no-padding correction"<<endl;
        }

        //Saving initial values
        Core_FromFile=Ztring().From_UTF8(Core_Get_Internal());

        //Data size check
        if (Chunks->Global->data && Chunks->Global->fmt_ && Chunks->Global->fmt_->formatType==0x0001)
        {
            int16u channelCount=Chunks->Global->fmt_->channelCount;
            int32u bytesPerSample=Chunks->Global->fmt_->bitsPerSample*8;
            if (Chunks->Global->data->Size%(channelCount*bytesPerSample)!=0)
            {

                ostringstream Message;
                Message <<"The audio is "<<channelCount<<" channels and " <<bytesPerSample<<" bytes per sample; "
                        <<"however, the data chunk is not aligned to a multiple of "
                        <<channelCount*bytesPerSample<<" ("<<channelCount<<"*"<<bytesPerSample << ").";
                Errors<<Chunks->Global->File_Name.To_UTF8()<<": "<<Message.str()<<endl;
                PerFile_Error.str(string());
                PerFile_Error<<Message.str()<<endl;
            }
        }

        //MD5
        if (Chunks->Global->VerifyMD5 || Chunks->Global->VerifyMD5_Force)
        {
            //Removing all MD5 related info
            Ztring PerFile_Error_Temp=Ztring().From_UTF8(PerFile_Error.str());
            PerFile_Error_Temp.FindAndReplace(Ztring("MD5, failed verification\n"), Ztring());
            PerFile_Error_Temp.FindAndReplace(Ztring("MD5, no existing MD5 chunk\n"), Ztring());
            PerFile_Error.str(PerFile_Error_Temp.To_UTF8());
            Ztring PerFile_Information_Temp=Ztring().From_UTF8(PerFile_Information.str());
            PerFile_Information_Temp.FindAndReplace(Ztring("MD5, no existing MD5 chunk\n"), Ztring());
            PerFile_Information_Temp.FindAndReplace(Ztring("MD5, verified\n"), Ztring());
            PerFile_Information.str(PerFile_Information_Temp.To_UTF8());
            
            //Checking
            if (!(Chunks->Global->MD5Stored && !Chunks->Global->MD5Stored->Strings["md5stored"].empty()))
            {
                if (Chunks->Global->VerifyMD5_Force)
                {
                    Errors<<Chunks->Global->File_Name.To_UTF8()<<": MD5, no existing MD5 chunk"<<endl;
                    PerFile_Error.str(string());
                    PerFile_Error<<"MD5, no existing MD5 chunk"<<endl;
                }
                else
                {
                    Information<<Chunks->Global->File_Name.To_UTF8()<<": MD5, no existing MD5 chunk"<<endl;
                    PerFile_Information<<"MD5, no existing MD5 chunk"<<endl;
                }
            }
            else if (Chunks->Global->MD5Generated && Chunks->Global->MD5Generated->Strings["md5generated"]!=Chunks->Global->MD5Stored->Strings["md5stored"])
            {
                Errors<<Chunks->Global->File_Name.To_UTF8()<<": MD5, failed verification"<<endl;
                PerFile_Error.str(string());
                PerFile_Error<<"MD5, failed verification"<<endl;
            }
            else
            {
                Information<<Chunks->Global->File_Name.To_UTF8()<<": MD5, verified"<<endl;
                PerFile_Information.str(string());
                PerFile_Information<<"MD5, verified"<<endl;
            }
            }
        if (EmbedMD5
         && Chunks->Global->MD5Generated && !Chunks->Global->MD5Generated->Strings["md5generated"].empty()
         && (!(Chunks->Global->MD5Stored && !Chunks->Global->MD5Stored->Strings["md5stored"].empty())
          || EmbedMD5_AuthorizeOverWritting))
                Set_Internal("MD5Stored", Chunks->Global->MD5Generated->Strings["md5generated"], rules());
    }

    CriticalSectionLocker(Chunks->Global->CS);
    Chunks->Global->Progress=1;
    
    return ReturnValue;
}

//---------------------------------------------------------------------------
bool Riff_Handler::Save()
{
    CriticalSectionLocker CSL(CS);

    Chunks->Global->CS.Enter();
    Chunks->Global->Progress=(float)0.05;
    Chunks->Global->CS.Leave();
    
    //Init
    PerFile_Error.str(string());

    //Integrity
    if (Chunks==NULL)
    {
        Errors<<"(No file name): Internal error"<<endl;
        return false;
    }

    if (IsReadOnly_Get_Internal())
    {
        Errors<<Chunks->Global->File_Name.To_UTF8()<<": Is read only"<<endl;
        return false;
    }

    //Write only if modified
    if (!IsModified_Get_Internal())
    {
        Information<<Chunks->Global->File_Name.To_UTF8()<<": Nothing to do"<<endl;
        return false;
    }

    //Modifying the chunks in memory
    for (size_t Fields_Pos=0; Fields_Pos<Fields_Max; Fields_Pos++)
        for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Pos]; Pos++)
        {
            if (!IsOriginal_Internal(xxxx_Strings[Fields_Pos][Pos], Get_Internal(xxxx_Strings[Fields_Pos][Pos])))
                Chunks->Modify(Elements::WAVE, Chunk_Name2_Get(xxxx_Strings[Fields_Pos][Pos]), Chunk_Name3_Get(xxxx_Strings[Fields_Pos][Pos]));
        }

    //File size management
    if (riff2rf64_Reject && Chunks && Chunks->Global->ds64==NULL && Chunks->Block_Size_Get()>RIFF_Size_Limit)
    {
        Errors<<Chunks->Global->File_Name.To_UTF8()<<": File size would be too big (and --reject-riff2rf64 option)"<<endl;
        PerFile_Error<<"File size would be too big (and --reject-riff2rf64 option)"<<endl;
        return false;
    }

    //Opening files
    if (!Chunks->Global->In.Open(Chunks->Global->File_Name))
    {
        Errors<<Chunks->Global->File_Name.To_UTF8()<<": File does not exist anymore"<<endl;
        PerFile_Error<<"File does not exist anymore"<<endl;
        return false;
    }

    //Old temporary file
    #if MACSTORE
    if (Chunks->Global->Temp_Path.size() && Chunks->Global->Temp_Name.size() && File::Exists(Chunks->Global->Temp_Path+Chunks->Global->Temp_Name) && !File::Delete(Chunks->Global->Temp_Path+Chunks->Global->Temp_Name))
    #else
    if (File::Exists(Chunks->Global->File_Name+__T(".tmp")) && !File::Delete(Chunks->Global->File_Name+__T(".tmp")))
    #endif
    {
        Errors<<Chunks->Global->File_Name.To_UTF8()<<": Old temporary file can't be deleted"<<endl;
        PerFile_Error<<"Old temporary file can't be deleted"<<endl;
        return false;
    }

    #ifdef MACSTORE
    if (Chunks->Global->Temp_Name.size())
        Chunks->Global->Temp_Name=__T("");

    if (Chunks->Global->Temp_Path.size())
    {
        if (Dir::Exists(Chunks->Global->Temp_Path))
            deleteTemporaryDirectory(Chunks->Global->Temp_Path);

        Chunks->Global->Temp_Path=__T("");
    }
    #endif
    //Parsing
    try
    {
        Chunks->Write();
    }
    catch (exception_canceled &)
    {
        Chunks->Global->Out.Close();
        #ifdef MACSTORE
        if (Chunks->Global->Temp_Path.size() && Chunks->Global->Temp_Name.size())
            File::Delete(Chunks->Global->Temp_Path+Chunks->Global->Temp_Name);

        if (Chunks->Global->Temp_Name.size())
            Chunks->Global->Temp_Name=__T("");

        if (Chunks->Global->Temp_Path.size())
        {
            if (Dir::Exists(Chunks->Global->Temp_Path))
                deleteTemporaryDirectory(Chunks->Global->Temp_Path);

            Chunks->Global->Temp_Path=__T("");
        }
        #else
        File::Delete(Chunks->Global->File_Name+__T(".tmp"));
        #endif
        CriticalSectionLocker(Chunks->Global->CS);
        File_IsCanceled=true;
        Chunks->Global->Canceling=false;
        return false;
    }
    catch (exception &e)
    {
        Chunks->Global->Out.Close();
        Errors<<Chunks->Global->File_Name.To_UTF8()<<": "<<e.what()<<endl;
        return false;
    }

    //Log
    Information<<(Chunks?Chunks->Global->File_Name.To_UTF8():"")<<": Is modified"<<endl;

    //Loading the new file (we are verifying the integraty of the generated file)
    string FileName=Chunks->Global->File_Name.To_UTF8();
    bool GenerateMD5_Temp=Chunks->Global->GenerateMD5;
    Chunks->Global->GenerateMD5=false;
    if (!Open_Internal(FileName) && Chunks==NULL) //There may be an error but file is open (eg MD5 error)
    {
        Errors<<FileName<<": WARNING, the resulting file can not be validated, file may be CORRUPTED"<<endl;
        PerFile_Error<<"WARNING, the resulting file can not be validated, file may be CORRUPTED"<<endl;
        Chunks->Global->GenerateMD5=GenerateMD5_Temp;
        return false;
    }
    Chunks->Global->GenerateMD5=GenerateMD5_Temp;

    CriticalSectionLocker(Chunks->Global->CS);
    Chunks->Global->Progress=1;

    return true;
}

//---------------------------------------------------------------------------
bool Riff_Handler::BackToLastSave()
{
    CriticalSectionLocker CSL(CS);
    for (size_t Fields_Pos=0; Fields_Pos<Fields_Max; Fields_Pos++)
        for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Pos]; Pos++)
        {
            if (!IsOriginal_Internal(xxxx_Strings[Fields_Pos][Pos], Get_Internal(xxxx_Strings[Fields_Pos][Pos])))
            {
                ZtringList HistoryList; HistoryList.Write(Ztring().From_UTF8(History(xxxx_Strings[Fields_Pos][Pos])));
                if (!HistoryList.empty())
                    Set_Internal(xxxx_Strings[Fields_Pos][Pos], HistoryList[0].To_UTF8(), rules());
            }
        }

    Chunks->IsModified_Clear();
        
    return true;
}

//***************************************************************************
// Per Item
//***************************************************************************

//---------------------------------------------------------------------------
string Riff_Handler::Get(const string &Field)
{
    CriticalSectionLocker CSL(CS);

    return Get_Internal(Field);
}

//---------------------------------------------------------------------------
string Riff_Handler::Get_Internal(const string &Field)
{
    //Special case - Technical fields
    if (Field=="SampleRate")
        return (((Chunks->Global->fmt_==NULL || Chunks->Global->fmt_->sampleRate    ==0)?"":Ztring::ToZtring(Chunks->Global->fmt_->sampleRate      ).To_UTF8()));
    else if (Field=="CodecID")
        return Chunks->Global->fmt_==NULL                                               ?"":Ztring().From_CC2(Chunks->Global->fmt_->formatType     ).To_UTF8();
    else if (Field=="BitsPerSample")
        return Chunks->Global->fmt_==NULL                                               ?"":Ztring::ToZtring(Chunks->Global->fmt_->bitsPerSample    ).To_UTF8();


    //Special case - CueXml
    if (Field=="cuexml")
        return Cue_Xml_Get();

    Riff_Base::global::chunk_strings** Chunk_Strings=chunk_strings_Get(Field);
    if (!Chunk_Strings || !*Chunk_Strings)
        return string();

    Ztring Value=Ztring().From_UTF8(Get(Field_Get(Field), *Chunk_Strings));
    Value.FindAndReplace(__T("\r\n"), __T("\n"), 0, Ztring_Recursive);
    Value.FindAndReplace(__T("\n\r"), __T("\n"), 0, Ztring_Recursive); //Bug in v0.2.1 XML, \r\n was inverted
    Value.FindAndReplace(__T("\r"), __T("\n"), 0, Ztring_Recursive);
    Value.FindAndReplace(__T("\n"), EOL, 0, Ztring_Recursive);
    
    return Value.To_UTF8();
}

//---------------------------------------------------------------------------
bool Riff_Handler::Set(const string &Field_, const string &Value_, rules Rules)
{
    CriticalSectionLocker CSL(CS);

    return Set_Internal(Field_, Value_, Rules);
}

//---------------------------------------------------------------------------
static const string aXML_Begin     ="<ebuCoreMain xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns=\"urn:ebu:metadata-schema:ebucore\">\r\n"
                                    "    <coreMetadata>\r\n";
static const string aXML_ISRC_Begin="        <identifier typeLabel=\"GUID\" typeDefinition=\"Globally Unique Identifier\" formatLabel=\"ISRC\" formatDefinition=\"International Standard Recording Code\" formatLink=\"http://www.ebu.ch/metadata/cs/ebu_IdentifierTypeCodeCS.xml#3.7\">\r\n"
                                    "            <dc:identifier>ISRC:";
static const string aXML_ISRC_End  =            "</dc:identifier>\r\n"
                                    "        </identifier>\r\n";
static const string aXML_End       ="    </coreMetadata>\r\n"
                                    "</ebuCoreMain>\r\n";
bool Riff_Handler::Set_Internal(const string &Field_, const string &Value_, rules Rules)
{
    //Integrity
    if (Chunks==NULL)
    {
        Errors<<"(No file name): Internal error"<<endl;
        return false;
    }

    string Field=Field_Get(Field_);
    
    //Testing if useful
    if (Field=="filename"
     || Field=="version"
     || Field=="errors"
     || Field=="information"
     || Value_=="NOCHANGE")
        return true;
    
    Ztring Value__=Ztring().From_UTF8(Value_);
    Value__.FindAndReplace(__T("\r\n"), __T("\n"), 0, Ztring_Recursive);
    Value__.FindAndReplace(__T("\n\r"), __T("\n"), 0, Ztring_Recursive); //Bug in v0.2.1 XML, \r\n was inverted
    Value__.FindAndReplace(__T("\r"), __T("\n"), 0, Ztring_Recursive);
    Value__.FindAndReplace(__T("\n"), __T("\r\n"), 0, Ztring_Recursive);

    if (Value__.size()>7
     && Value__[0]==__T('f')
     && Value__[1]==__T('i')
     && Value__[2]==__T('l')
     && Value__[3]==__T('e')
     && Value__[4]==__T(':')
     && Value__[5]==__T('/')
     && Value__[6]==__T('/'))
    {
        File F;
        if (!F.Open(Value__.substr(7, string::npos)))
        {
            Errors<<Chunks->Global->File_Name.To_UTF8()<<": Malformed input ("<<Field<<"="<<Value__.To_UTF8()<<", File does not exist)"<<endl;
            return false;
        }

        int64u F_Size=F.Size_Get();
        if (F_Size>((size_t)-1)-1)
        {
            Errors<<Chunks->Global->File_Name.To_UTF8()<<": Malformed input ("<<Field<<"="<<Value__.To_UTF8()<<", Unable to open file)"<<endl;
            return false;
        }

        //Creating buffer
        int8u* Buffer=new int8u[(size_t)F_Size+1];
        size_t Buffer_Offset=0;

        //Reading the file
        while(Buffer_Offset<F_Size)
        {
            size_t BytesRead=F.Read(Buffer+Buffer_Offset, (size_t)F_Size-Buffer_Offset);
            if (BytesRead==0)
                break; //Read is finished
            Buffer_Offset+=BytesRead;
        }
        if (Buffer_Offset<F_Size)
        {
            Errors<<Chunks->Global->File_Name.To_UTF8()<<": Malformed input ("<<Field<<"="<<Value__.To_UTF8()<<", Error while reading file)"<<endl;
            delete[] Buffer;
            return false;
        }
        Buffer[Buffer_Offset]='\0';

        Value__=Ztring().From_UTF8((const char*)Buffer);
    }
    string Value=Value__.To_UTF8();

    //Legacy
    if (Field=="timereference" && !(Value.size()<12
          ||  Value[Value.size()-12]< '0' || Value[Value.size()-12]> '9' 
          ||  Value[Value.size()-11]< '0' || Value[Value.size()-11]> '9' 
          || (Value[Value.size()-10]!='-' && Value[Value.size()-10]!='_' && Value[Value.size()-10]!=':' && Value[Value.size()-10]!=' ' && Value[Value.size()-10]!='.') 
          ||  Value[Value.size()- 9]< '0' || Value[Value.size()- 9]> '5' 
          ||  Value[Value.size()- 8]< '0' || Value[Value.size()- 8]> '9' 
          || (Value[Value.size()- 7]!='-' && Value[Value.size()- 7]!='_' && Value[Value.size()- 7]!=':' && Value[Value.size()- 7]!=' ' && Value[Value.size()- 7]!='.') 
          ||  Value[Value.size()- 6]< '0' || Value[Value.size()- 6]> '5' 
          ||  Value[Value.size()- 5]< '0' || Value[Value.size()- 5]> '9' 
          || (Value[Value.size()- 4]!='-' && Value[Value.size()- 4]!='_' && Value[Value.size()- 4]!=':' && Value[Value.size()- 4]!=' ' && Value[Value.size()- 4]!='.') 
          ||  Value[Value.size()- 3]< '0' || Value[Value.size()- 3]> '9' 
          ||  Value[Value.size()- 2]< '0' || Value[Value.size()- 2]> '9' 
          ||  Value[Value.size()- 1]< '0' || Value[Value.size()- 1]> '9')) 
        Field="timereference (translated)";

    // EBU ISRC recommandations, link aXML ISRC and INFO ISRC
    string FieldToFill, ValueToFill;
    if (Rules.EBU_ISRC_Rec)
    {
        if (Field=="ISRC")
        {
            string OldISRC=Get_Internal("ISRC");
            if (Value!=OldISRC)
            {
                string aXML=Get_Internal("aXML");
                if (aXML.empty())
                {
                    aXML =aXML_Begin;
                    aXML+=aXML_ISRC_Begin;
                    aXML+=Value;
                    aXML+=aXML_ISRC_End;
                    aXML+=aXML_End;
                    FieldToFill="aXML";
                    ValueToFill=aXML;
                }
                else
                {
                    size_t Begin=aXML.find(aXML_ISRC_Begin);
                    size_t End=aXML.find(aXML_ISRC_End, Begin);
                    if (Begin!=string::npos && End!=string::npos && End<=Begin+aXML_ISRC_Begin.size()+20)
                    {
                        string OldISRC_aXML=aXML.substr(Begin+aXML_ISRC_Begin.size(), End-(Begin+aXML_ISRC_Begin.size()));
                        string OldISRC_INFO=Get_Internal("ISRC");
                        if (OldISRC_aXML.empty() || OldISRC_INFO==OldISRC_aXML)
                        {
                            if (Value.empty())
                            {
                                aXML.erase(Begin, End+aXML_ISRC_End.size()-Begin);
                                if (aXML==aXML_Begin+aXML_End)
                                    aXML.clear();
                            }
                            else
                            {
                                aXML.erase(Begin+aXML_ISRC_Begin.size(), End-(Begin+aXML_ISRC_Begin.size()));
                                aXML.insert(Begin+aXML_ISRC_Begin.size(), Value);
                            }
                            FieldToFill="aXML";
                            ValueToFill=aXML;
                        }
                    }
                    else if (aXML.size()>=aXML_End.size() && !aXML.compare(aXML.size()-aXML_End.size(), aXML_End.size(), aXML_End))
                    {
                        aXML.insert(aXML.size()-aXML_End.size(), aXML_ISRC_Begin+Value+aXML_ISRC_End);
                        FieldToFill="aXML";
                        ValueToFill=aXML;
                    }

                }
            }
        }
        if (Field=="axml")
        {
            string OldaXML=Get_Internal("aXML");
            if (!Value.empty() && Value!=OldaXML)
            {
                string NewISRC_aXML;
                size_t Begin=Value.find(aXML_ISRC_Begin);
                size_t End=Value.find(aXML_ISRC_End, Begin);
                if (Begin!=string::npos && End!=string::npos && End<=Begin+aXML_ISRC_Begin.size()+20)
                    NewISRC_aXML=Value.substr(Begin+aXML_ISRC_Begin.size(), End-(Begin+aXML_ISRC_Begin.size()));
                string OldISRC_aXML;
                Begin=OldaXML.find(aXML_ISRC_Begin);
                End=OldaXML.find(aXML_ISRC_End, Begin);
                if (Begin!=string::npos && End!=string::npos && End<=Begin+aXML_ISRC_Begin.size()+20)
                    OldISRC_aXML=OldaXML.substr(Begin+aXML_ISRC_Begin.size(), End-(Begin+aXML_ISRC_Begin.size()));
                if (!NewISRC_aXML.empty() && NewISRC_aXML!=OldISRC_aXML)
                {
                    string OldISRC_INFO=Get_Internal("ISRC");
                    if (OldISRC_INFO.empty() || OldISRC_INFO==OldISRC_aXML)
                    {
                        FieldToFill="ISRC";
                        ValueToFill=NewISRC_aXML;
                    }
                }
            }
        }
    }

    //Testing validity
    if (!IsValid_Internal(Field, Value, Rules, true)
     && !IsOriginal_Internal(Field, Value))
        return false;

    //Special cases - before
    if (Field=="timereference (translated)")
    {
        if (Value.empty())
            return Set_Internal("timereference", string(), Rules);
        else if (Value.size()>=12 && Chunks && Chunks->Global && Chunks->Global->fmt_ && Chunks->Global->fmt_->sampleRate)
        {
            int64u HH=Ztring().From_UTF8(Value.substr(0, Value.size()-10)).To_int64u();
            int64u MM=Ztring().From_UTF8(Value.substr(Value.size()-9, 2 )).To_int64u();
            int64u SS=Ztring().From_UTF8(Value.substr(Value.size()-6, 2 )).To_int64u();
            int64u MS=Ztring().From_UTF8(Value.substr(Value.size()-3, 3 )).To_int64u();
            int64u TimeReference;
            TimeReference=HH*60*60*1000
                        + MM*   60*1000
                        + SS*      1000
                        + MS;
            TimeReference=(int64u)(((float64)TimeReference)/1000*Chunks->Global->fmt_->sampleRate);
            
            if (Value!=Get_Internal("timereference (translated)"))
                return Set_Internal("timereference", Ztring().From_Number(TimeReference).To_UTF8(), Rules);
            else
                return true;
        }
    }
    if (Field=="timereference" && Value=="0")
        Value.clear();
    if (Field=="bext")
        return Set_Internal("bextversion", Value, Rules);

    //Special case - CueXml
    if (Field=="cuexml")
        return Cue_Xml_Set(Value, Rules);

    //Setting it
    Riff_Base::global::chunk_strings** Chunk_Strings=chunk_strings_Get(Field);
    if (!Chunk_Strings)
        return false;

    bool ToReturn=Set(Field, Value, *Chunk_Strings, Chunk_Name2_Get(Field), Chunk_Name3_Get(Field));

    //Special cases - After
    if (ToReturn && Field=="originator")
    {
        if (Rules.FADGI_Rec && Chunks && Chunks->Global && Chunks->Global->INFO)
            Set_Internal("IARL", Value, Rules); //If INFO is present, IARL is filled with the same value
    }
    if (!FieldToFill.empty())
    {
        Set_Internal(FieldToFill, ValueToFill, Rules);
    }

    return ToReturn;
}

//---------------------------------------------------------------------------
bool Riff_Handler::Remove(const string &Field)
{
    CriticalSectionLocker CSL(CS);

    //Integrity
    if (Chunks==NULL)
    {
        Errors<<"(No file name): Internal error"<<endl;
        return false;
    }

    //bext + INFO
    if (Ztring().From_UTF8(Field).MakeLowerCase()==__T("core"))
    {
        bool ToReturn=true;
        for (size_t Fields_Pos=Fields_Bext; Fields_Pos<=Fields_Info; Fields_Pos++) //Only Bext and Info
            for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Pos]; Pos++)
            {
                Riff_Base::global::chunk_strings** Chunk_Strings=chunk_strings_Get(xxxx_Strings[Fields_Pos][Pos]);
                if (!Chunk_Strings)
                {
                    ToReturn=false;
                    continue;
                }
                if (!Set(Field_Get(xxxx_Strings[Fields_Pos][Pos]), string(), *Chunk_Strings, Chunk_Name2_Get(xxxx_Strings[Fields_Pos][Pos]), Chunk_Name3_Get(xxxx_Strings[Fields_Pos][Pos])))
                    ToReturn=false;
            }

        return ToReturn;
    }

    //Special case: CueXml
    if (Ztring().From_UTF8(Field).MakeLowerCase()==__T("cuexml"))
        return Cue_Xml_Set("", rules());

    Riff_Base::global::chunk_strings** Chunk_Strings=chunk_strings_Get(Field);
    if (!Chunk_Strings)
        return false;

    return Set(Field, string(), *Chunk_Strings, Chunk_Name2_Get(Field), Chunk_Name3_Get(Field));
}

//---------------------------------------------------------------------------
bool Riff_Handler::IsModified(const string &Field)
{
    CriticalSectionLocker CSL(CS);

    return IsModified_Internal(Field);
}

//---------------------------------------------------------------------------
bool Riff_Handler::IsModified_Internal(const string &Field)
{
    //Special cases
    if (Field_Get(Field)=="bext")
    {
        bool ToReturn=false;
        for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Bext]; Pos++)
             if (IsModified_Internal(xxxx_Strings[Fields_Bext][Pos]))
                 ToReturn=true;
        if (!ToReturn && IsModified_Internal("bextversion"))
             ToReturn=true;
        return ToReturn;
    }
    if (Field_Get(Field)=="INFO")
    {
        bool ToReturn=false;
        for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Info]; Pos++)
             if (IsModified_Internal(xxxx_Strings[Fields_Info][Pos]))
                 ToReturn=true;
        return ToReturn;
    }
    if (Field_Get(Field)=="cuexml")
    {
        bool ToReturn=false;
        ToReturn|=IsModified_Internal("cue");
        ToReturn|=IsModified_Internal("labl");
        ToReturn|=IsModified_Internal("note");
        ToReturn|=IsModified_Internal("ltxt");
        return ToReturn;
    }


    Riff_Base::global::chunk_strings** Chunk_Strings=chunk_strings_Get(Field);
    if (!Chunk_Strings || !*Chunk_Strings)
        return false;
    
    return IsModified(Field_Get(Field=="timereference (translated)"?string("timereference"):Field), *Chunk_Strings);
}

//---------------------------------------------------------------------------
bool Riff_Handler::IsValid(const string &Field_, const string &Value_, rules Rules, bool IgnoreCoherency)
{
    CriticalSectionLocker CSL(CS);

    return IsValid_Internal(Field_, Value_, Rules, IgnoreCoherency);
}
//---------------------------------------------------------------------------
bool Riff_Handler::IsValid_Internal(const string &Field_, const string &Value_, rules Rules, bool IgnoreCoherency)
{
    //Reformating
    IsValid_Errors.str(string());
    IsValid_Warnings.str(string());
    string Field=Field_Get(Field_);
    Ztring Value__=Ztring().From_UTF8(Value_);
    Value__.FindAndReplace(__T("\r\n"), __T("\n"), 0, Ztring_Recursive);
    Value__.FindAndReplace(__T("\n\r"), __T("\n"), 0, Ztring_Recursive); //Bug in v0.2.1 XML, \r\n was inverted
    Value__.FindAndReplace(__T("\r"), __T("\n"), 0, Ztring_Recursive);
    Value__.FindAndReplace(__T("\n"), __T("\r\n"), 0, Ztring_Recursive);
    string Value=Value__.To_UTF8();

    //Rules
    if (Rules.FADGI_Rec)
        Rules.Tech3285_Rec=true;
    if (Rules.Tech3285_Rec)
        Rules.Tech3285_Req=true;
    if (Rules.INFO_Rec)
        Rules.INFO_Req=true;

    //Encoding (warning only)
    if (Field!="filename" && Field!="errors" && Field!="information")
    {
        bool IsASCII=true;
        for (size_t i=0; i<Value.size(); i++)
        {
            if (((unsigned char)Value[i]) >= 0x80)
            {
                IsASCII = false;
                break;
            }
        }
        if (!IsASCII)
        {
            IsValid_Warnings<<"Warning: due to the presence of non ASCII characters, ";
            if (Field=="description" || Field=="originator" || Field=="originatorreference")
                IsValid_Warnings<<"which are forbidden by EBU Text 3285 specifications, ";
            else
                IsValid_Warnings<<"whose the encoding in files is not specified in WAV documents, ";
            IsValid_Warnings<<"there is a risk of interoperability issues with other tools (including BWF MetaEdit on other platforms) "
                            <<"as the content is converted with local codepage";
        }
    }

    //FileName
    if (Field=="filename")
    {
        //Test
        string Message;
        if (!PerFile_Error.str().empty())
            Message=PerFile_Error.str();

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed file, "<<Message;
    }

    //MD5Stored
    if (Field=="md5stored")
    {
        //Test
        string Message;
        for (size_t Pos=0; Pos<Value.size(); Pos++)
            if (!((Value[Pos]>='0' && Value[Pos]<='9') || (Value[Pos]>='a' && Value[Pos]<='f') || (Value[Pos]>='A' && Value[Pos]<='F')))
                Message="allowed characters are 0-9, a-z, A-Z";
        if (Message.empty())
        {
            if (!Value.empty() && Value.size()!=32)
                Message="must be 16 byte long hexadecimal coded text (32 byte long) value";
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, MD5Stored "<<Message;
    }

    //MD5Stored
    else if (Field=="md5generated")
    {
        //Test
        string Message;
        for (size_t Pos=0; Pos<Value.size(); Pos++)
            if (!((Value[Pos]>='0' && Value[Pos]<='9') || (Value[Pos]>='A' && Value[Pos]<='F') || (Value[Pos]>='A' && Value[Pos]<='F')))
                Message="allowed characters are 0-9, a-z, A-Z";
        if (Message.empty())
        {
            if (!Value.empty() && Value.size()!=32)
                Message="must be 16 byte long hexadecimal coded text (32 byte long) value";
            else if (!Value.empty() && !Get_Internal("MD5Stored").empty() && Value!=Get_Internal("MD5Stored"))
                Message="Failed verification";
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, MD5Generated "<<Message;
    }

    //Description
    else if (Field=="description")
    {
        //Test
        string Message;
        if (Value.size()>256) //Limitation from the format
            Message="length is maximum 256 characters (format limitation)";
        if (Rules.FADGI_Rec && Value.empty())
            Message="must not be empty (FADGI recommandations)";

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, Description "<<Message;
    }

    //Originator
    else if (Field=="originator")
    {
        //Test
        string Message;
        if (Value.size()>32) //Limitation from the format
            Message="length is maximum 32 characters (format limitation)";
        else if (Rules.FADGI_Rec)
        {
            if (Value.empty())
                Message="must not be empty (FADGI recommandations)";
            else if (Value.size()>0 && (Value[0] < 'A' || Value[0] > 'Z'))
                Message="1st character must be between 'A' and 'Z' (FADGI recommandations)";
            else if (Value.size()>1 && (Value[1] < 'A' || Value[1] > 'Z'))
                Message="2nd character must be between 'A' and 'Z' (FADGI recommandations)";
            else if (Value.size()>2 && Value[2] !=',')
                Message="3rd character must be a comma (FADGI recommandations)";
            else if (Value.size()>3 && Value[3] !=' ')
                Message="4th character must be a space (FADGI recommandations)";
            else if (Value.size()<5)
                Message="length is minimum 5 characters (FADGI recommandations)";
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, Originator "<<Message;
    }

    //OriginatorReference
    else if (Field=="originatorreference")
    {
        //Test
        string Message;
        if (Value.size()>32) //Limitation from the format
            Message="length is maximum 32 characters (format limitation)";
        else if (Rules.FADGI_Rec && Value.empty())
            Message="must not be empty (FADGI recommandations)";
        else if (Rules.OriginatorReference_Rec)
        {
            if (Value.size()>1 && (Value[0] < 'A' || Value[0] > 'Z'))  //Country code
                Message="1st character (Country code) must be between 'A' and 'Z' (BWF OriginatorReference recommandations)";
            else if (Value.size()>1 && (Value[1] < 'A' || Value[1] > 'Z'))  //Country code
                Message="2nd character (Country code) must be between 'A' and 'Z' (BWF OriginatorReference recommandations)";
            else if (Value.size()>2 && (Value[2] < '0' || (Value[2] > '9' && Value[2] < 'A') || Value[2] > 'Z'))  //Organisation code
                Message="3rd character (Organisation code) must be between '0' and '9' or 'A' and 'Z' (BWF OriginatorReference recommandations)";
            else if (Value.size()>3 && (Value[3] < '0' || (Value[3] > '9' && Value[3] < 'A') || Value[3] > 'Z'))  //Organisation code
                Message="4th character (Organisation code) must be between '0' and '9' or 'A' and 'Z' (BWF OriginatorReference recommandations)";
            else if (Value.size()>4 && (Value[4] < '0' || (Value[4] > '9' && Value[4] < 'A') || Value[4] > 'Z'))  //Organisation code
                Message="5th character (Organisation code) must be between '0' and '9' or 'A' and 'Z' (BWF OriginatorReference recommandations)";
            else if (Value.size()>17 && (Value[17]< '0' || Value[17]> '2'))  //OriginationTime
                Message="18th and 19th character (OriginationTime) must be between '00' and '23' (BWF OriginatorReference recommandations)";
            else if (Value.size()>18 && (Value[18]< '0' || (Value[18]> (Value[16]=='2'?'3':'9')))) //Only 00-23 //OriginationTime
                Message="18th and 19th character (OriginationTime) must be between '00' and '23' (BWF OriginatorReference recommandations)";
            else if (Value.size()>19 && (Value[19]< '0' || Value[19]> '5'))  //OriginationTime
                Message="20th and 21st character (OriginationTime) must be between '00' and '59' (BWF OriginatorReference recommandations)";
            else if (Value.size()>20 && (Value[20]< '0' || Value[20]> '9'))  //OriginationTime
                Message="20th and 21st character (OriginationTime) must be between '00' and '59' (BWF OriginatorReference recommandations)";
            else if (Value.size()>21 && (Value[21]< '0' || Value[21]> '9'))  //OriginationTime
                Message="22nd and 23rd character (OriginationTime) must be between '00' and '59' (BWF OriginatorReference recommandations)";
            else if (Value.size()>22 && (Value[22]< '0' || Value[22]> '9'))  //OriginationTime
                Message="22nd and 23rd character (OriginationTime) must be between '00' and '59' (BWF OriginatorReference recommandations)";
            else if (Value.size()>23 && (Value[23]< '0' || Value[23]> '9'))  //Random Number
                Message="24th character (Random Number) must be between '0' and '9' (BWF OriginatorReference recommandations)";
            else if (Value.size()>24 && (Value[24]< '0' || Value[24]> '9'))  //Random Number
                Message="25th character (Random Number) must be between '0' and '9' (BWF OriginatorReference recommandations)";
            else if (Value.size()>25 && (Value[25]< '0' || Value[25]> '9'))  //Random Number
                Message="26th character (Random Number) must be between '0' and '9' (BWF OriginatorReference recommandations)";
            else if (Value.size()>26 && (Value[26]< '0' || Value[26]> '9'))  //Random Number
                Message="27th character (Random Number) must be between '0' and '9' (BWF OriginatorReference recommandations)";
            else if (Value.size()>27 && (Value[27]< '0' || Value[27]> '9'))  //Random Number
                Message="28th character (Random Number) must be between '0' and '9' (BWF OriginatorReference recommandations)";
            else if (Value.size()>28 && (Value[28]< '0' || Value[28]> '9'))  //Random Number
                Message="29th character (Random Number) must be between '0' and '9' (BWF OriginatorReference recommandations)";
            else if (Value.size()>29 && (Value[29]< '0' || Value[29]> '9'))  //Random Number
                Message="30th character (Random Number) must be between '0' and '9' (BWF OriginatorReference recommandations)";
            else if (Value.size()>30 && (Value[30]< '0' || Value[30]> '9'))  //Random Number
                Message="31st character (Random Number) must be between '0' and '9' (BWF OriginatorReference recommandations)";
            else if (Value.size()>31 && (Value[31]< '0' || Value[31]> '9'))  //Random Number
                Message="32nd character (Random Number) must be between '0' and '9' (BWF OriginatorReference recommandations)";
            else if (Value.size()<32)
                Message="length must be 32 characters long (BWF OriginatorReference recommandations)";
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, OriginatorReference "<<Message;
    }

    //OriginationDate
    else if (Field=="originationdate")
    {
        //Test
        string Message;
        if (Value.size()>10)
            Message="length is maximum 10 characters (format limitation)";
        else if (Rules.FADGI_Rec)
        {
            if (Value.empty())
                Message="must not be empty (FADGI recommandations)";
            else if (Value.size()!=4 && Value.size()!=7 && Value.size()!=9 && Value.size()!=10)
                Message="must be YYYY, YYYY--YYYY, YYYY/YYYY, YYYY-MM, or YYYY-MM-DD (FADGI recommandations)";
			else
            {
                     if (Value[0]< '0' || Value[0]> '9') //Year
                    Message="1st to 4th characters (Year) must be between '0000' and '9999' (BWF requirements)";
                else if (Value[1]< '0' || Value[1]> '9') //Year
                    Message="1st to 4th characters (Year) must be between '0000' and '9999' (BWF requirements)";
                else if (Value[2]< '0' || Value[2]> '9') //Year
                    Message="1st to 4th characters (Year) must be between '0000' and '9999' (BWF requirements)";
                else if (Value[3]< '0' || Value[3]> '9') //Year
                    Message="1st to 4th characters (Year) must be between '0000' and '9999' (BWF requirements)";
                else if ((Value.size()==9 && Value[4]=='/') || (Value.size()==10 && Value[4]=='-' && Value[5]=='-')) //Year range
                {
                     size_t Start=Value[4]=='/'?5:6;
                     if (Value[Start]< '0' || Value[Start]> '9') //Year
                        Message=string(Start==5?"5th to 8th":"6th to 10th")+" characters (Year) must be between '0000' and '9999' (FADGI recommandations)";
                    else if (Value[Start+1]< '0' || Value[Start+1]> '9') //Year
                        Message=string(Start==5?"5th to 8th":"6th to 10th")+" characters (Year) must be between '0000' and '9999' (FADGI recommandations)";
                    else if (Value[Start+2]< '0' || Value[Start+2]> '9') //Year
                        Message=string(Start==5?"5th to 8th":"6th to 10th")+" characters (Year) must be between '0000' and '9999' (FADGI recommandations)";
                    else if (Value[Start+3]< '0' || Value[Start+3]> '9') //Year
                        Message=string(Start==5?"5th to 8th":"6th to 10th")+" characters (Year) must be between '0000' and '9999' (FADGI recommandations)";
                }
                else if (Value.size()>=7) //YYYY-DD
                {
                         if (Value[4]!='-') //Hyphen
                        Message="5th character must be '-' (FADGI recommandations)";
                    else if (Value[5]< '0' || Value[5]> '1') //Month
                        Message="6th and 7th characters (Month) must be between '01' and '12' (BWF requirements)";
                    else if ((Value[6]< (Value[5]=='0'?'1':'0')) || (Value[6]> (Value[5]=='1'?'2':'9'))) //Only 01-12 //Month
                        Message="6th and 7th characters (Month) must be between '01' and '12' (BWF requirements)";
                    else if (Value.size()==10) //YYYY-DD-MM
                    {
                             if (Value[7]!='-') //Hyphen
                            Message="8th character must be '-' (FADGI recommandations)";
                        else if (Value[8]< '0' || Value[8]> '3') //Day
                            Message="9th and 10th characters (Day) must be between '01' and '23' (BWF requirements)";
                        else if ((Value[9]< (Value[8]=='0'?'1':'0')) || (Value[9]> (Value[8]=='3'?'1':'9'))) //Only 01-31 //Day
                            Message="9th and 10th characters (Day) must be between '01' and '23' (BWF requirements)";
                    }
                }
            }
        }
        else if (Rules.Tech3285_Req && !Value.empty())
        {
                 if (Value.size()!=10)
                Message="must be YYYY-MM-DD (BWF requirements)";
            else if (Value[0]< '0' || Value[0]> '9') //Year
                Message="1st to 4th characters (Year) must be between '0000' and '9999' (BWF requirements)";
            else if (Value[1]< '0' || Value[1]> '9') //Year
                Message="1st to 4th characters (Year) must be between '0000' and '9999' (BWF requirements)";
            else if (Value[2]< '0' || Value[2]> '9') //Year
                Message="1st to 4th characters (Year) must be between '0000' and '9999' (BWF requirements)";
            else if (Value[3]< '0' || Value[3]> '9') //Year
                Message="1st to 4th characters (Year) must be between '0000' and '9999' (BWF requirements)";
            else if (Rules.Tech3285_Rec && Value[4]!='-' && Value[4]!='_' && Value[4]!=':' && Value[4]!=' ' && Value[4]!='.') //Hyphen 
                Message="5th character must be '-', '_', ':', ' ', or '.' (BWF recommandation)";
            else if (Value[5]< '0' || Value[5]> '1') //Month
                Message="6th and 7th characters (Month) must be between '01' and '12' (BWF requirements)";
            else if ((Value[6]< (Value[5]=='0'?'1':'0')) || (Value[6]> (Value[5]=='1'?'2':'9'))) //Only 01-12 //Month
                Message="6th and 7th characters (Month) must be between '01' and '12' (BWF requirements)";
            else if (Rules.Tech3285_Rec && Value[7]!='-' && Value[7]!='_' && Value[7]!=':' && Value[7]!=' ' && Value[7]!='.') //Hyphen 
                Message="8th character must be '-', '_', ':', ' ', or '.' (BWF recommendations)";
            else if (Value[8]< '0' || Value[8]> '3') //Day
                Message="9th and 10th characters (Day) must be between '01' and '23' (BWF requirements)";
            else if ((Value[9]< (Value[8]=='0'?'1':'0')) || (Value[9]> (Value[8]=='3'?'1':'9'))) //Only 01-31 //Day
                Message="9th and 10th characters (Day) must be between '01' and '23' (BWF requirements)";
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, OriginationDate "<<Message;
    }

    else if (Field=="originationtime")
    {
        //Test
        string Message;
        if (Value.size()>8)
            Message="length is maximum 8 characters (format limitation)";
		else if (Rules.FADGI_Rec && !Value.empty())
        {
        //    if (Value.empty())
        //        Message="must not be empty (FADGI recommandations)";
            if (Value.size()!=2 && Value.size()!=5 && Value.size()!=8)
                Message="must be HH, HH:MM, or HH:MM:SS (FADGI recommandations)";
            else
            {
                     if (Value[0]< '0' || Value[0]> '2') //Hours
                    Message="1st and 2nd characters (Hours) must be between '00' and '23' (FADGI recommandations)";
                else if (Value[1]< '0' || (Value[1]> (Value[0]=='2'?'3':'9'))) //Only 00-23 //Hours
                    Message="1st and 2nd characters (Hours) must be between '00' and '23' (FADGI recommandations)";
                else if (Value.size()>=5) //HH:MM
                {
                         if (Value[2]!=':') //Separator
                        Message="3rd character must be ':' (FADGI recommandations)";
                    else if (Value[3]< '0' || Value[3]> '5') //Minutes
                        Message="4th and 5th characters (Minutes) must be between '00' and '59' (FADGI recommandations)";
                    else if (Value[4]< '0' || Value[4]> '9' ) //Minutes
                        Message="4th and 5th characters (Minutes) must be between '00' and '59' (FADGI recommandations)";
                    else if (Value.size()==8) //HH:MM:SS
                    {
                             if (Value[5]!=':') //Separator
                            Message="6th character must be ':' (FADGI recommandations)";
                        else if (Value[6]< '0' || Value[6]> '5') //Seconds
                            Message="7th and 8th characters (Seconds) must be between '00' and '59' (FADGI recommandations)";
                        else if (Value[7]< '0' || Value[7]> '9' ) //Seconds
                            Message="7th and 8th characters (Seconds) must be between '00' and '59' (FADGI recommandations)";
                    }
                }
            }
        }
        else if (Rules.Tech3285_Req && !Value.empty())
        {
                 if (Value.size()!=8)
                     Message="must be HH:MM:SS";
            else if (Value[0]< '0' || Value[0]> '2') //Hours
                Message="1st and 2nd characters (Hours) must be between '00' and '23'";
            else if (Value[1]< '0' || (Value[1]> (Value[0]=='2'?'3':'9'))) //Only 00-23 //Hours
                Message="1st and 2nd characters (Hours) must be between '00' and '23'";
            else if (Rules.Tech3285_Rec && Value[2]!='-' && Value[2]!='_' && Value[2]!=':' && Value[2]!=' ' && Value[2]!='.') //Separator 
                Message="3rd character must be '-', '_', ':', ' ', or '.' (BWF recommendations)";
            else if (Value[3]< '0' || Value[3]> '5') //Minutes
                Message="4th and 5th characters (Minutes) must be between '00' and '59'";
            else if (Value[4]< '0' || Value[4]> '9' ) //Minutes
                Message="4th and 5th characters (Minutes) must be between '00' and '59'";
            else if (Rules.Tech3285_Rec && Value[5]!='-' && Value[5]!='_' && Value[5]!=':' && Value[5]!=' ' && Value[5]!='.') //Separator 
                Message="6th character must be '-', '_', ':', ' ', or '.' (BWF recommendations)";
            else if (Value[6]< '0' || Value[6]> '5') //Seconds
                Message="7th and 8th characters (Seconds) must be between '00' and '59'";
            else if (Value[7]< '0' || Value[7]> '9' ) //Seconds
                Message="7th and 8th characters (Seconds) must be between '00' and '59'";
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, OriginationTime "<<Message;
    }

    else if (Field=="timereference (translated)")
    {
        //Test
        string Message;
        if (Value.empty())
            {}
        else if (Value.size()<12
          ||  Value[Value.size()-12]< '0' || Value[Value.size()-12]> '9' 
          ||  Value[Value.size()-11]< '0' || Value[Value.size()-11]> '9' 
          || (Value[Value.size()-10]!='-' && Value[Value.size()-10]!='_' && Value[Value.size()-10]!=':' && Value[Value.size()-10]!=' ' && Value[Value.size()-10]!='.') 
          ||  Value[Value.size()- 9]< '0' || Value[Value.size()- 9]> '5' 
          ||  Value[Value.size()- 8]< '0' || Value[Value.size()- 8]> '9' 
          || (Value[Value.size()- 7]!='-' && Value[Value.size()- 7]!='_' && Value[Value.size()- 7]!=':' && Value[Value.size()- 7]!=' ' && Value[Value.size()- 7]!='.') 
          ||  Value[Value.size()- 6]< '0' || Value[Value.size()- 6]> '5' 
          ||  Value[Value.size()- 5]< '0' || Value[Value.size()- 5]> '9' 
          || (Value[Value.size()- 4]!='-' && Value[Value.size()- 4]!='_' && Value[Value.size()- 4]!=':' && Value[Value.size()- 4]!=' ' && Value[Value.size()- 4]!='.') 
          ||  Value[Value.size()- 3]< '0' || Value[Value.size()- 3]> '9' 
          ||  Value[Value.size()- 2]< '0' || Value[Value.size()- 2]> '9' 
          ||  Value[Value.size()- 1]< '0' || Value[Value.size()- 1]> '9')
                Message="format must be HH:MM:SS.mmm";

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, TimeReference (translated) "<<Message;
    }

    else if (Field=="timereference")
    {
        //Test
        string Message;
        if (Value.size()>18)
            Message="must be a number less than 18 digits";
        else
            for (size_t Pos=0; Pos<Value.size(); Pos++)
                if (Value[Pos]<'0' || Value[Pos]>'9')
                    Message="must be a number";

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, TimeReference "<<Message;
    }

    else if (Field=="BextVersion")
    {
        //Test
        string Message;
        if (!Value.empty() && Value!="0" && Value!="1" && Value!="2")
            Message="must be empty, 0, 1 or 2";

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, BextVersion "<<Message;
    }

    else if (Field=="umid")
    {
        //Test
        string Message;
        /* Old method disabled
        if (Value.find('-')!=string::npos)
        {
            //Old method
            if (!Value.empty() && Ztring(Value).To_UUID()==0 && Value!="00000000-0000-0000-0000-000000000000")
                Message="must be XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX and it must contain only 0-9 and A-F (legacy method)";
        }
        else */
        {
            if (!Value.empty() && Value.size()!=64 && Value.size()!=128)
                Message="must be 0-, 32- or 64-byte long";
            else
            {
                for (size_t Pos=0; Pos<Value.size(); Pos++)
                    if (!((Value[Pos]>='0' && Value[Pos]<='9') || (Value[Pos]>='a' && Value[Pos]<='f') || (Value[Pos]>='A' && Value[Pos]<='F')))
                    {
                        Message="must contain only 0-9 and A-F (hexadecimal)";
                        break;
                    }
                if (Message.empty()) 
                {
                    
                }
            }
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, UMID "<<Message;
    }

    else if (Field=="bext_version")
    {
        //Test
        string Message;
        if (!Value.empty() && Value!="0" && Value!="1" && Value!="2")
            Message="must be empty, 0, 1 or 2";

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, bext version "<<Message;
    }

    else if (Field=="loudnessvalue" || Field=="loudnessrange" || Field=="maxtruepeaklevel" || Field=="maxmomentaryloudness" || Field=="maxshorttermloudness")
    {
        //Test
        string Message;
        size_t Minus=(!Value.empty() && Value[0]=='-')?1:0;
        size_t Decimal=Value.find('.');
        if (Value.empty())
            ;
        else if (Rules.Tech3285_Req)
        {
            if (Value.size()>=Minus+1 && (Value[Minus]<'0' || Value[Minus]>'9'))
                Message="must be XX.XX or -XX.XX";
            if (Value.size()>=Minus+2 && Decimal!=Minus+2 && (Value[Minus+1]<'0' && Value[Minus+1]>'9'))
                Message="must be XX.XX or -XX.XX";
            if (Value.size()>=Minus+3 && Decimal!=Minus+3 && (Value[Minus+2]<'0' && Value[Minus+2]>'9'))
                Message="must be XX.XX or -XX.XX";
            if (Value.size()>=Minus+4 && (Value[Minus+3]<'0' && Value[Minus+3]>'9'))
                Message="must be XX.XX or -XX.XX";
            if (Value.size()>=Minus+5 && (Value[Minus+4]<'0' && Value[Minus+4]>'9'))
                Message="must be XX.XX or -XX.XX";
            if (Value.size()>=Minus+6)
                Message="must be XX.XX or -XX.XX";
            if (Field=="loudnessrange" && Minus)
                Message="must be XX.XX or -XX.XX";
        }
        else
        {
            if (Value.size()>=Minus+1 && (Value[Minus]<'0' || Value[Minus]>'9'))
                Message="must be XXX.XX or -XXX.XX";
            if (Value.size()>=Minus+2 && Decimal!=Minus+2 && (Value[Minus+1]<'0' && Value[Minus+1]>'9'))
                Message="must be XXX.XX or -XXX.XX";
            if (Value.size()>=Minus+3 && Decimal!=Minus+3 && (Value[Minus+2]<'0' && Value[Minus+2]>'9'))
                Message="must be XXX.XX or -XXX.XX";
            if (Value.size()>=Minus+4 && Decimal!=Minus+4 && (Value[Minus+3]<'0' && Value[Minus+3]>'9'))
                Message="must be XXX.XX or -XXX.XX";
            if (Value.size()>=Minus+5 && (Value[Minus+4]<'0' && Value[Minus+4]>'9'))
                Message="must be XXX.XX or -XXX.XX";
            if (Value.size()>=Minus+6 && (Value[Minus+5]<'0' && Value[Minus+5]>'9'))
                Message="must be XXX.XX or -XXX.XX";
            if (Value.size()>=Minus+7)
                Message="must be XXX.XX or -XXX.XX";
            if (Field=="loudnessrange" && Minus)
                Message="must be positive value";
            if (Message.empty())
            {
                float32 Float=Ztring().From_UTF8(Value).To_float32();
                if (Float<=-327.68 || Float>=327.69)
                {
                    if (Field=="loudnessrange")
                        Message="must be between 0 and 327.68";
                    else
                        Message="must be between -655.35 and 327.68";
                }
                int32s SavedValue=float32_int32s(Float*100);
                if (SavedValue<-32767 || SavedValue>32768)
                {
                    if (Field=="loudnessrange")
                        Message="must be 0 or 327.68";
                    else
                        Message="must be -327.67 or 327.68";
                }
            }
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, "<<Field<<" "<<Message;
    }

    else if (Field=="codinghistory")
    {
        //Test
        string Message;
        if (Rules.CodingHistory_Rec || Rules.FADGI_Rec)
        {
            //Loading
            bool Wrong=false;

            ZtringList Lines;
            Lines.Separator_Set(0, __T("\r\n"));
            Lines.Write(Ztring().From_UTF8(Value));
            for (size_t Line_Pos=0; Line_Pos<Lines.size(); Line_Pos++)
            {
                ZtringList Data;
                Data.Separator_Set(0, __T(","));
                Data.Write(Lines[Line_Pos]);
                for (size_t Data_Pos=0; Data_Pos<Data.size(); Data_Pos++)
                {
                    int Column=-1;
                    Ztring &Value=Data[Data_Pos];
                    if (Value.size()>=2 && Value[1]==__T('='))
                    {
                        switch (Value[0])
                        {
                            case __T('A') : Column=0; break;
                            case __T('F') : Column=1; break;
                            case __T('B') : Column=2; break;
                            case __T('W') : Column=3; break;
                            case __T('M') : Column=4; break;
                            case __T('T') : Column=5; break;
                            default  : Wrong=true;
                        }
                    }
                    else
                        Wrong=true;
                    if (Column==Data_Pos)
                    {
                        Value.erase(0, 2);
                        switch (Column)
                        {
                            case 0 :    if (Value!=__T("")
                                         && Value!=__T("ANALOGUE")
                                         && Value!=__T("PCM")
                                         && Value!=__T("MPEG1L1")
                                         && Value!=__T("MPEG1L2")
                                         && Value!=__T("MPEG1L3")
                                         && Value!=__T("MPEG2L1")
                                         && Value!=__T("MPEG2L2")
                                         && Value!=__T("MPEG2L3"))
                                        Wrong=true;
                                        break;
                                     

                            case 1 :    if (Value!=__T("")
                                         && Value!=__T("11000")
                                         && Value!=__T("22050")
                                         && Value!=__T("24000")
                                         && Value!=__T("32000")
                                         && Value!=__T("44100")
                                         && Value!=__T("48000")
                                         && Value!=__T("64000")
                                         && Value!=__T("88200")
                                         && ((!Rules.FADGI_Rec || Rules.CodingHistory_Rec)
                                         || (Value!=__T("96000")
                                         &&  Value!=__T("176400")
                                         &&  Value!=__T("192000")
                                         &&  Value!=__T("384000")
                                         &&  Value!=__T("768000"))))
                                        Wrong=true;
                                        break;

                            case 2 :    if (Value!=__T("")
                                         && Value!=__T("8")
                                         && Value!=__T("16")
                                         && Value!=__T("24")
                                         && Value!=__T("32")
                                         && Value!=__T("40")
                                         && Value!=__T("48")
                                         && Value!=__T("56")
                                         && Value!=__T("64")
                                         && Value!=__T("80")
                                         && Value!=__T("96")
                                         && Value!=__T("112")
                                         && Value!=__T("128")
                                         && Value!=__T("144")
                                         && Value!=__T("160")
                                         && Value!=__T("176")
                                         && Value!=__T("192")
                                         && Value!=__T("224")
                                         && Value!=__T("256")
                                         && Value!=__T("320")
                                         && Value!=__T("352")
                                         && Value!=__T("384")
                                         && Value!=__T("416")
                                         && Value!=__T("448"))
                                        Wrong=true;
                                        break;

                            case 3 :    if (Value!=__T("")
                                         && Value!=__T("8")
                                         && Value!=__T("12")
                                         && Value!=__T("14")
                                         && Value!=__T("16")
                                         && Value!=__T("18")
                                         && Value!=__T("20")
                                         && Value!=__T("22")
                                         && Value!=__T("24")
                                         && ((!Rules.FADGI_Rec || Rules.CodingHistory_Rec)
                                         || (Value!=__T("32"))))
                                        Wrong=true;
                                        break;

                            case 4 :    if (Value!=__T("")
                                         && Value!=__T("mono")
                                         && Value!=__T("stereo")
                                         && Value!=__T("dual-mono")
                                         && Value!=__T("joint-stereo")
                                         && ((!Rules.FADGI_Rec || Rules.CodingHistory_Rec)
                                         || (Value!=__T("multitrack")
                                         &&  Value!=__T("multichannel")
                                         &&  Value!=__T("surround")
                                         &&  Value!=__T("other"))))
                                        Wrong=true;
                                        break;
                            default :   ;
                        }
                    }
                    else if (Column!=-1 || Data_Pos>5)
                    {
                        Data.insert(Data.begin()+Data_Pos, Ztring());
                        if (Data.size()>6)
                        {
                            Wrong=true;
                            break;
                        }
                    }
                }
                if (Lines[Line_Pos].size()>0 && Lines[Line_Pos][Lines[Line_Pos].size()-1]==__T(','))
                    Message="comma at the end of line";
            }
            
            if (Wrong)
                Message="does not respect rules ";
            else if (!Value.empty() && (Value.size()<2 || Value[Value.size()-2]!=__T('\r') || Value[Value.size()-1]!=__T('\n') ))
                Message="does not terminate with \\r\\n";
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, CodingHistory "<<Message;
    }

    else if (Field=="IARL")
    {
        //Test
        string Message;
        if (Rules.FADGI_Rec && Chunks)
        {
                 if (Value.empty() && Chunks->Global->bext && !Chunks->Global->bext->Strings["originator"].empty())
                Message="must equal originator (FADGI recommandations)";
            else if (!Value.empty() && !Chunks->Global->bext)
                Message="must equal originator (FADGI recommandations)";
            else if (!Value.empty() && Chunks->Global->bext && Value!=Chunks->Global->bext->Strings["originator"])
                Message="must equal originator (FADGI recommandations)";
            else if (Value.empty() && Chunks->Global->INFO)
                Message="must not be empty (FADGI recommandations)";
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input (IARL "<<Message<<")";
    }

    else if (Field=="ICMT" && Rules.INFO_Req)
    {
        if (Value.find_first_of("\r\n")!=string::npos)
        {
            Errors<<(Chunks?Chunks->Global->File_Name.To_UTF8():"")<<": malformed input (ICMT="<<Value<<", carriage return are not acceptable)"<<endl;
            return false;
        }
    }

    //ICRD
    else if (Field=="ICRD")
    {
        //Test
        string Message;
        if (Rules.INFO_Rec || Rules.FADGI_Rec)
        {
            string Rule = Rules.INFO_Rec ? "(INFO Recommandations)" : "(FADGI Recommandations)";
            if (Value.empty())
                {}
            else if (Value.size()<=10 && Value.size()!=4 && Value.size()!=7 && Value.size()!=9 && Value.size()!=10)
                Message="must be YYYY, YYYY--YYYY, YYYY/YYYY, YYYY-MM, or YYYY-MM-DD or YYYY-MM-DD+junk " + Rule;
            else
            {
                     if (Value[0]< '0' || Value[0]> '9') //Year
                    Message="1st to 4th characters (Year) must be between '0000' and '9999' " + Rule;
                else if (Value[1]< '0' || Value[1]> '9') //Year
                    Message="1st to 4th characters (Year) must be between '0000' and '9999' " + Rule;
                else if (Value[2]< '0' || Value[2]> '9') //Year
                    Message="1st to 4th characters (Year) must be between '0000' and '9999' " + Rule;
                else if (Value[3]< '0' || Value[3]> '9') //Year
                    Message="1st to 4th characters (Year) must be between '0000' and '9999' " + Rule;
                else if (Rules.FADGI_Rec && ((Value.size()>=9 && Value[4]=='/') || (Value.size()>=10 && Value[4]=='-' && Value[5]=='-'))) //Year range
                {
                     size_t Start=Value[4]=='/'?5:6;
                     if (Value[Start]< '0' || Value[Start]> '9') //Year
                        Message=string(Start==5?"5th to 8th":"6th to 10th")+" characters (Year) must be between '0000' and '9999' " + Rule;
                    else if (Value[Start+1]< '0' || Value[Start+1]> '9') //Year
                        Message=string(Start==5?"5th to 8th":"6th to 10th")+" characters (Year) must be between '0000' and '9999' " + Rule;
                    else if (Value[Start+2]< '0' || Value[Start+2]> '9') //Year
                        Message=string(Start==5?"5th to 8th":"6th to 10th")+" characters (Year) must be between '0000' and '9999' " + Rule;
                    else if (Value[Start+3]< '0' || Value[Start+3]> '9') //Year
                        Message=string(Start==5?"5th to 8th":"6th to 10th")+" characters (Year) must be between '0000' and '9999' " + Rule;
                }
                else if (Value.size()>=7) //YYYY-DD
                {
                         if (Value[4]!='-') //Hyphen
                        Message="5th character must be '-' " + Rule;
                    else if (Value[5]< '0' || Value[5]> '1') //Month
                        Message="6th and 7th characters (Month) must be between '01' and '12' " + Rule;
                    else if ((Value[6]< (Value[5]=='0'?'1':'0')) || (Value[6]> (Value[5]=='1'?'2':'9'))) //Only 01-12 //Month
                        Message="6th and 7th characters (Month) must be between '01' and '12' " + Rule;
                    else if (Value.size()>=10) //YYYY-DD-MM
                    {
                             if (Value[7]!='-') //Hyphen
                            Message="8th character must be '-' " + Rule;
                        else if (Value[8]< '0' || Value[8]> '3') //Day
                            Message="9th and 10th characters (Day) must be between '01' and '23' " + Rule;
                        else if ((Value[9]< (Value[8]=='0'?'1':'0')) || (Value[9]> (Value[8]=='3'?'1':'9'))) //Only 01-31 //Day
                            Message="9th and 10th characters (Day) must be between '01' and '23' " + Rule;
                    }
                }
            }
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, ICRD "<<Message;
    }

    //ISRC
    else if (Field=="ISRC")
    {
        //Test
        string Message;
        if (Rules.EBU_ISRC_Rec && !IgnoreCoherency)
        {
            //From aXML
            string aXML=Get_Internal("aXML");
            string aXML_ISRC;
            size_t Begin=aXML.find(aXML_ISRC_Begin);
            size_t End=aXML.find(aXML_ISRC_End, Begin);
            if (Begin!=string::npos && End!=string::npos && End<=Begin+aXML_ISRC_Begin.size()+20)
                aXML_ISRC=aXML.substr(Begin+aXML_ISRC_Begin.size(), End-(Begin+aXML_ISRC_Begin.size()));

            //From INFO
            if (!aXML_ISRC.empty() && !Value.empty() && aXML_ISRC!=Value)
            {
                Message="is not same between aXML and INFO chunks";
            }
        }

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, ISRC "<<Message;
    }

    //ICRD
    else if (Field=="md5generated")
    {
        //Test
        string Message;
        if (!(Chunks->Global->MD5Generated && !Chunks->Global->MD5Generated->Strings["md5generated"].empty()) && !(Chunks->Global->MD5Stored && !Chunks->Global->MD5Stored->Strings["md5stored"].empty()) && Chunks->Global->MD5Generated->Strings["md5generated"]!=Chunks->Global->MD5Stored->Strings["md5stored"])
            Message="does not equal MD5Stored";

        //If error
        if (!Message.empty()) 
            IsValid_Errors<<"malformed input, MD5Generated "<<Message;
    }

    else if (Field=="axml" || Field=="ixml" || Field=="xmp")
    {
        tinyxml2::XMLDocument Document;
        if (!Value.empty() && Document.Parse(Value.c_str())!=tinyxml2::XML_SUCCESS)
            IsValid_Warnings<<"xml validation error "<<Document.ErrorName()<<" at line "<<Ztring().From_Number(Document.ErrorLineNum()).To_UTF8();
    }

    else if (Field=="cuexml")
    {
        string cue_;
        string labl;
        string note;
        string ltxt;

        Cue_Xml_To_Fields(Value, cue_, labl, note, ltxt);

        ZtringListList Cues(Ztring().From_UTF8(cue_));
        ZtringListList Labels(Ztring().From_UTF8(labl));
        ZtringListList Notes(Ztring().From_UTF8(note));
        ZtringListList Texts(Ztring().From_UTF8(ltxt));

        //cue_
        {
            ZtringList Points;
            ZtringList Duplicates;
            for (size_t Pos=0; Pos<Cues.size(); Pos++)
            {
                Ztring Point=Cues[Pos](0);
                if (find(Points.begin(), Points.end(), Point)!=Points.end())
                {
                    Duplicates.push_back(Point);
                    continue;
                }
                Points.push_back(Point);
            }
            if (!Duplicates.empty())
            {
                IsValid_Errors<<"Errors: duplicate cue point"<<(Duplicates.size()>1?"s: ":": ")<<Duplicates[0].To_UTF8();
                for (size_t Pos=1; Pos<Duplicates.size(); Pos++)
                    IsValid_Errors<<", "<<Duplicates[Pos].To_UTF8();
            }
        }

        //labl
        {
            ZtringList Points;
            ZtringList Missing;
            ZtringList Duplicates;
            for (size_t Pos=0; Pos<Labels.size(); Pos++)
            {
                Ztring Point=Labels[Pos](0);

                if (Cues.Find(Point, 0, 0)==Error)
                    Missing.push_back(Point);
                else if (Points.Find(Point)!=Error)
                    Duplicates.push_back(Point);
                else
                    Points.push_back(Point);
            }
            if (!Missing.empty())
            {
                if (!IsValid_Errors.str().empty())
                    IsValid_Errors<<endl;
                IsValid_Errors<<"Error: missing cue point"<<(Missing.size()>1?"s: ":": ")<<Missing[0].To_UTF8();
                for (size_t Pos=1; Pos<Missing.size(); Pos++)
                    IsValid_Errors<<", "<<Missing[Pos].To_UTF8();
                IsValid_Errors<<(Missing.size()>1?" are":" is")<<" referenced by labl "<<(Missing.size()>1?"entries":"entry");
            }
            if (!Duplicates.empty())
            {
                if (!IsValid_Errors.str().empty())
                    IsValid_Errors<<endl;
                IsValid_Errors<<"Error: multiple labl entries reference the same cue point"<<(Duplicates.size()>1?"s: ":": ")<<Duplicates[0].To_UTF8();
                for (size_t Pos=1; Pos<Duplicates.size(); Pos++)
                    IsValid_Errors<<", "<<Duplicates[Pos].To_UTF8();
            }
        }

        //note
        {
            ZtringList Points;
            ZtringList Missing;
            ZtringList Duplicates;
            for (size_t Pos=0; Pos<Notes.size(); Pos++)
            {
                Ztring Point=Notes[Pos](0);
                if (Cues.Find(Point, 0, 0)==Error)
                    Missing.push_back(Point);
                else if (Points.Find(Point)!=Error)
                    Duplicates.push_back(Point);
                else
                    Points.push_back(Point);
            }
            if (!Missing.empty())
            {
                if (!IsValid_Errors.str().empty())
                    IsValid_Errors<<endl;
                IsValid_Errors<<"Error: missing cue point"<<(Missing.size()>1?"s: ":": ")<<Missing[0].To_UTF8();
                for (size_t Pos=1; Pos<Missing.size(); Pos++)
                    IsValid_Errors<<", "<<Missing[Pos].To_UTF8();
                IsValid_Errors<<(Missing.size()>1?" are":" is")<<" referenced by note "<<(Missing.size()>1?"entries":"entry");
            }
            if (!Duplicates.empty())
            {
                if (!IsValid_Errors.str().empty())
                    IsValid_Errors<<endl;
                IsValid_Errors<<"Error: multiple note entries reference the same cue point"<<(Duplicates.size()>1?"s: ":": ")<<Duplicates[0].To_UTF8();
                for (size_t Pos=1; Pos<Duplicates.size(); Pos++)
                    IsValid_Errors<<", "<<Duplicates[Pos].To_UTF8();
            }
        }

        //ltxt
        {
            ZtringList Points;
            ZtringList Missing;
            ZtringList Duplicates;
            ostringstream IsValid_Errors_Save(IsValid_Errors.str());
            ostringstream IsValid_Warnings_Save(IsValid_Warnings.str());
            for (size_t Pos=0; Pos<Texts.size(); Pos++)
            {
                Ztring Point=Texts[Pos](0);
                if (Cues.Find(Point, 0, 0)==Error)
                    Missing.push_back(Point);
                else if (Points.Find(Point)!=Error)
                    Duplicates.push_back(Point);
                else
                    Points.push_back(Point);

                if (!IgnoreCoherency)
                {
                    string Purpose=Texts[Pos](2).To_UTF8();
                    string Country=Texts[Pos](3).To_UTF8();
                    string Language=Texts[Pos](4).To_UTF8();
                    string Dialect=Texts[Pos](5).To_UTF8();

                    IsValid_Internal("cue_ltxt_purpose", Purpose, Rules, IgnoreCoherency);

                    if (!IsValid_Errors.str().empty() && !IsValid_Errors_Save.str().empty())
                        IsValid_Errors_Save<<endl;
                    IsValid_Errors_Save<<IsValid_Errors.str();

                    if (!IsValid_Warnings.str().empty() && !IsValid_Warnings_Save.str().empty())
                        IsValid_Warnings_Save<<endl;
                    IsValid_Warnings_Save<<IsValid_Warnings.str();


                    IsValid_Internal("cue_ltxt_country", Country, Rules, IgnoreCoherency);

                    if (!IsValid_Errors.str().empty() && !IsValid_Errors_Save.str().empty())
                        IsValid_Errors_Save<<endl;
                    IsValid_Errors_Save<<IsValid_Errors.str();

                    if (!IsValid_Warnings.str().empty() && !IsValid_Warnings_Save.str().empty())
                        IsValid_Warnings_Save<<endl;
                    IsValid_Warnings_Save<<IsValid_Warnings.str();

                    IsValid_Internal("cue_ltxt_language", Language, Rules, IgnoreCoherency);

                    if (!IsValid_Errors.str().empty() && !IsValid_Errors_Save.str().empty())
                        IsValid_Errors_Save<<endl;
                    IsValid_Errors_Save<<IsValid_Errors.str();

                    if (!IsValid_Warnings.str().empty() && !IsValid_Warnings_Save.str().empty())
                        IsValid_Warnings_Save<<endl;
                    IsValid_Warnings_Save<<IsValid_Warnings.str();

                    IsValid_Internal("cue_ltxt_language_dialect", Language + ";" + Dialect, Rules, IgnoreCoherency);

                    if (!IsValid_Errors.str().empty() && !IsValid_Errors_Save.str().empty())
                        IsValid_Errors_Save<<endl;
                    IsValid_Errors_Save<<IsValid_Errors.str();

                    if (!IsValid_Warnings.str().empty() && !IsValid_Warnings_Save.str().empty())
                        IsValid_Warnings_Save<<endl;
                    IsValid_Warnings_Save<<IsValid_Warnings.str();
                }
                IsValid_Errors.str(IsValid_Errors_Save.str());
                IsValid_Warnings.str(IsValid_Warnings_Save.str());
            }
            if (!Missing.empty())
            {
                if (!IsValid_Errors.str().empty())
                    IsValid_Errors<<endl;
                IsValid_Errors<<"Error: missing cue point"<<(Missing.size()>1?"s: ":": ")<<Missing[0].To_UTF8();
                for (size_t Pos=1; Pos<Missing.size(); Pos++)
                    IsValid_Errors<<", "<<Missing[Pos].To_UTF8();
                IsValid_Errors<<(Missing.size()>1?" are":" is")<<" referenced by ltxt "<<(Missing.size()>1?"entries":"entry");
            }
            if (!Duplicates.empty())
            {
                if (!IsValid_Errors.str().empty())
                    IsValid_Errors<<endl;
                IsValid_Errors<<"Error: multiple ltxt entries reference the same cue point"<<(Duplicates.size()>1?"s: ":": ")<<Duplicates[0].To_UTF8();
                for (size_t Pos=1; Pos<Duplicates.size(); Pos++)
                    IsValid_Errors<<", "<<Duplicates[Pos].To_UTF8();
            }
        }
    }

    else if (Field=="cue_labl")
    {
        if (Rules.FADGI_Rec && Value.empty())
            IsValid_Errors<<"Label is mandatory for each cue point (FADGI recommandations)";
    }

    else if (Field=="cue_ltxt_purpose")
    {
        int32u PurposeID=Ztring().From_UTF8(Value).To_int32u(16);
        if (Rules.FADGI_Rec && PurposeID!=0)
            if (PurposeID!=0x73706561 && PurposeID!=0x656E7669 && PurposeID!=0x6E6F7465 && PurposeID!=0x7472616E && PurposeID!=0x6F746872)
                IsValid_Errors<<"Purpose ID must be one of: 0, spea, envi, note, tran, othr (FADGI recommandations)";
    }

    else if (Field=="cue_ltxt_country")
    {
        int16u Code=Ztring().From_UTF8(Value).To_int16u();
        if (Rules.FADGI_Rec && Code!=0)
        {
            if (IsISOCountry(Code))
                IsValid_Warnings<<"ISO 3366-1 for country is not officialy supported by FADGI recommandations";
            else if (!IsCSETCountry(Code))
                IsValid_Errors<<"Country must be a valid CSET country code (FADGI recommandations)";
        }
    }

    else if (Field=="cue_ltxt_language")
    {
        int16u Code=Ztring().From_UTF8(Value).To_int16u();
        if (Rules.FADGI_Rec && Code!=0)
        {
            if (IsISOLanguage(Code))
                IsValid_Warnings<<"ISO 639-1 for language is not officialy supported by FADGI recommandations";
            else if (!IsCSETLanguage(Code))
                IsValid_Errors<<"Language must be a valid CSET language code (FADGI recommandations)";
        }
    }

    else if (Field=="cue_ltxt_language_dialect")
    {
        ZtringList Codes=ZtringList(Ztring().From_UTF8(Value));
        int16u Language=Codes(0).To_int16u();
        int16u Code=Codes(1).To_int16u();
        if (Rules.FADGI_Rec && (Language || Code))
        {
            if (IsISOLanguage(Language))
            {
                if (Code)
                    IsValid_Errors<<"Dialect must be set to zero with ISO 639-1 language code";
            }
            else if (!IsCSETDialect(Language, Code))
                IsValid_Errors<<"Dialect must be a valid CSET dialect code for the given language (FADGI recommandations)";
        }
    }

    else if (Field=="errors")
    {
        return PerFile_Error.str().empty();
    }

    else if (Field=="information")
    {
        return true;
    }

    if (!IsValid_Errors.str().empty())
    {
        Errors<<(Chunks?Chunks->Global->File_Name.To_UTF8():"")<<": "<<IsValid_Errors.str()<<endl;
        return false;
    }
    else
        return true;
}

//---------------------------------------------------------------------------
bool Riff_Handler::IsOriginal(const string &Field, const string &Value)
{
    CriticalSectionLocker CSL(CS);

    return IsOriginal_Internal(Field, Value);
}

//---------------------------------------------------------------------------
bool Riff_Handler::IsOriginal_Internal(const string &Field, const string &Value)
{
    //Special case: CueXml
    if (Field=="cuexml")
    {
        string cue_;
        string labl;
        string note;
        string ltxt;

        if (!Cue_Xml_To_Fields(Value, cue_, labl, note, ltxt))
            return false;

        bool ToReturn=true;
        ToReturn&=IsOriginal_Internal("cue", cue_);
        ToReturn&=IsOriginal_Internal("labl", cue_);
        ToReturn&=IsOriginal_Internal("note", cue_);
        ToReturn&=IsOriginal_Internal("ltxt", cue_);

        return ToReturn;
    }

    Riff_Base::global::chunk_strings** Chunk_Strings=chunk_strings_Get(Field);
    if (!Chunk_Strings || !*Chunk_Strings)
        return true;
    
    return IsOriginal(Field_Get(Field=="timereference (translated)"?Ztring("timereference").To_UTF8():Field), Value, *Chunk_Strings);
}

//---------------------------------------------------------------------------
string Riff_Handler::History(const string &Field)
{
    CriticalSectionLocker CSL(CS);

    //Special case: CueXml
    if(Field=="cuexml")
        return string();

    Riff_Base::global::chunk_strings** Chunk_Strings=chunk_strings_Get(Field);
    if (!Chunk_Strings || !*Chunk_Strings)
        return string();
    
    return History(Field_Get(Field), *Chunk_Strings);
}

//***************************************************************************
// Global
//***************************************************************************

//---------------------------------------------------------------------------
string Riff_Handler::Core_Header()
{
    string ToReturn;
    ToReturn+="FileName,";
    for (size_t Fields_Pos=Fields_Bext; Fields_Pos<=Fields_Info; Fields_Pos++) //Only Bext and Info
        for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Pos]; Pos++)
        {
            ToReturn+=xxxx_Strings[Fields_Pos][Pos];
            ToReturn+=',';
        }

    //Remove the extra ","
    if (!ToReturn.empty())
        ToReturn.resize(ToReturn.size()-1);

    return ToReturn;
}

//---------------------------------------------------------------------------
string Riff_Handler::Core_Get(bool Batch_IsBackuping)
{
    CriticalSectionLocker CSL(CS);

    return Core_Get_Internal(Batch_IsBackuping);
}

//---------------------------------------------------------------------------
string Riff_Handler::Core_Get_Internal(bool Batch_IsBackuping)
{
    //FromFile
    if (Batch_IsBackuping)
        return Core_FromFile.To_UTF8();
    
    ZtringList List;
    List.Separator_Set(0, ",");

    List.push_back(Chunks->Global->File_Name);
    for (size_t Fields_Pos=Fields_Bext; Fields_Pos<=Fields_Info; Fields_Pos++) //Only Bext and Info
        for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Pos]; Pos++)
             List.push_back(Ztring().From_UTF8(Get_Internal(xxxx_Strings[Fields_Pos][Pos])));

    return List.Read().To_UTF8();
}

//---------------------------------------------------------------------------
string Riff_Handler::Technical_Header()
{
    ostringstream ToReturn;
    ToReturn<<"FileName"<<',';
    ToReturn<<"FileSize"<<',';
    ToReturn<<"Format"<<',';
    ToReturn<<"CodecID"<<',';
    ToReturn<<"Channels"<<',';
    ToReturn<<"SampleRate"<<',';
    ToReturn<<"BitRate"<<',';
    ToReturn<<"BitPerSample"<<',';
    ToReturn<<"Duration"<<',';
    ToReturn<<"UnsupportedChunks"<<",";
    ToReturn<<"bext"<<',';
    ToReturn<<"INFO"<<',';
    ToReturn<<"Cue"<<',';
    ToReturn<<"XMP"<<',';
    ToReturn<<"aXML"<<',';
    ToReturn<<"iXML"<<',';
    ToReturn<<"MD5Stored"<<',';
    ToReturn<<"MD5Generated"<<',';
    ToReturn<<"Errors"<<',';
    ToReturn<<"Information";

    return ToReturn.str();
}

//---------------------------------------------------------------------------
string Riff_Handler::Technical_Get()
{
    CriticalSectionLocker CSL(CS);

    ZtringList List;
    List.Separator_Set(0, __T(","));
    List.push_back(Chunks->Global->File_Name);
    List.push_back(Ztring::ToZtring(Chunks->Global->File_Size));
    List.push_back(Chunks->Global->IsRF64?__T("Wave (RF64)"):__T("Wave"));
    List.push_back( Chunks->Global->fmt_==NULL                                            ?__T(""):Ztring().From_CC2(Chunks->Global->fmt_->formatType      ));
    List.push_back(((Chunks->Global->fmt_==NULL || Chunks->Global->fmt_->channelCount  ==0)?__T(""):Ztring::ToZtring(Chunks->Global->fmt_->channelCount    )));
    List.push_back(((Chunks->Global->fmt_==NULL || Chunks->Global->fmt_->sampleRate    ==0)?__T(""):Ztring::ToZtring(Chunks->Global->fmt_->sampleRate      )));
    List.push_back(((Chunks->Global->fmt_==NULL || Chunks->Global->fmt_->bytesPerSecond==0)?__T(""):Ztring::ToZtring(Chunks->Global->fmt_->bytesPerSecond*8)));
    List.push_back(((Chunks->Global->fmt_==NULL || Chunks->Global->fmt_->bitsPerSample ==0)?__T(""):Ztring::ToZtring(Chunks->Global->fmt_->bitsPerSample   )));
    List.push_back(((Chunks->Global->fmt_==NULL || Chunks->Global->fmt_->bytesPerSecond==0 || Chunks->Global->data==NULL || Chunks->Global->data->Size==(int64u)-1)?__T(""):Ztring().Duration_From_Milliseconds(Chunks->Global->data->Size*1000/Chunks->Global->fmt_->bytesPerSecond)));
    List.push_back(Ztring().From_UTF8(Chunks->Global->UnsupportedChunks));
    if (Chunks->Global->bext!=NULL && !Chunks->Global->bext->Strings["bextversion"].empty())
        List.push_back(Get_Internal("bext").empty()?__T("No"):(__T("Version ")+Ztring().From_UTF8(Chunks->Global->bext->Strings["bextversion"])));
    else
        List.push_back(__T("No"));
    List.push_back(Get_Internal("INFO").empty()?__T("No"):__T("Yes"));
    List.push_back(Get_Internal("cuexml").empty()?__T("No"):__T("Yes"));
    List.push_back(Get_Internal("XMP").empty()?__T("No"):__T("Yes"));
    List.push_back(Get_Internal("aXML").empty()?__T("No"):__T("Yes"));
    List.push_back(Get_Internal("iXML").empty()?__T("No"):__T("Yes"));
    List.push_back(Ztring().From_UTF8(Get_Internal("MD5Stored")));
    List.push_back(Ztring().From_UTF8(Get_Internal("MD5Generated")));
    string Errors_Temp=PerFile_Error.str();
    if (!Errors_Temp.empty())
        Errors_Temp.resize(Errors_Temp.size()-1);
    List.push_back(Ztring().From_UTF8(Errors_Temp));
    string Information_Temp=PerFile_Information.str();
    if (!Information_Temp.empty())
        Information_Temp.resize(Information_Temp.size()-1);
    List.push_back(Ztring().From_UTF8(Information_Temp));
    return List.Read().To_UTF8();
}

//***************************************************************************
// Info
//***************************************************************************

//---------------------------------------------------------------------------
string Riff_Handler::Trace_Get()
{
    CriticalSectionLocker CSL(CS);

    return Chunks->Global->Trace.str();
}

//---------------------------------------------------------------------------
string Riff_Handler::FileName_Get()
{
    CriticalSectionLocker CSL(CS);

    return Chunks->Global->File_Name.To_UTF8();
}

//---------------------------------------------------------------------------
string Riff_Handler::FileDate_Get()
{
    CriticalSectionLocker CSL(CS);

    return Chunks->Global->File_Date;
}

//---------------------------------------------------------------------------
float Riff_Handler::Progress_Get()
{
    CriticalSectionLocker CSL(CS);

    if (Chunks==NULL || Chunks->Global==NULL)
        return 0;    
    CriticalSectionLocker(Chunks->Global->CS);
    if (Chunks->Global->Progress==1)
        return (float)0.99; //Must finish opening, see Open()
    return Chunks->Global->Progress;
}

//---------------------------------------------------------------------------
void Riff_Handler::Progress_Clear()
{
    CriticalSectionLocker CSL(CS);

    if (Chunks==NULL || Chunks->Global==NULL)
        return;

    CriticalSectionLocker(Chunks->Global->CS);
    Chunks->Global->Progress=0;
}

//---------------------------------------------------------------------------
bool Riff_Handler::Canceled_Get()
{
    CriticalSectionLocker CSL(CS);

    if (Chunks==NULL || Chunks->Global==NULL)
        return false;    
    CriticalSectionLocker(Chunks->Global->CS);
    return File_IsCanceled;
}

//---------------------------------------------------------------------------
void Riff_Handler::Cancel()
{
    CriticalSectionLocker CSL(CS);

    if (Chunks==NULL || Chunks->Global==NULL)
        return;    
    CriticalSectionLocker(Chunks->Global->CS);
    Chunks->Global->Canceling=true;
}

//---------------------------------------------------------------------------
bool Riff_Handler::IsModified_Get()
{
    CriticalSectionLocker CSL(CS);

    return IsModified_Get_Internal();
}

//---------------------------------------------------------------------------
bool Riff_Handler::IsModified_Get_Internal()
{
    bool ToReturn=false;
    for (size_t Fields_Pos=0; Fields_Pos<Fields_Max; Fields_Pos++)
        for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Pos]; Pos++)
            if (IsModified_Internal(xxxx_Strings[Fields_Pos][Pos]))
                ToReturn=true;

    if (!ToReturn && Chunks)
        ToReturn=Chunks->IsModified();

    return ToReturn;
}

//---------------------------------------------------------------------------
bool Riff_Handler::IsReadOnly_Get()
{
    CriticalSectionLocker CSL(CS);

    return IsReadOnly_Get_Internal();
}

//---------------------------------------------------------------------------
bool Riff_Handler::IsReadOnly_Get_Internal()
{
    if (Chunks==NULL || Chunks->Global==NULL)
        return false;

    return Chunks->Global->Read_Only;
}


//---------------------------------------------------------------------------
bool Riff_Handler::IsValid_Get()
{
    CriticalSectionLocker CSL(CS);

    return File_IsValid;
}

//***************************************************************************
// Helpers - Per item
//***************************************************************************

//---------------------------------------------------------------------------
string Riff_Handler::Get(const string &Field, Riff_Base::global::chunk_strings* &Chunk_Strings)
{
    if (Field=="errors")
        return PerFile_Error.str();
    if (Field=="information")
        return PerFile_Information.str();
    if (Field=="sample rate" ||Field=="samplerate")
        return (((Chunks->Global->fmt_==NULL || Chunks->Global->fmt_->sampleRate    ==0)?"":Ztring::ToZtring(Chunks->Global->fmt_->sampleRate      ).To_UTF8()));

    //Special cases
    if (Field=="bext" && &Chunk_Strings && Chunk_Strings)
    {
        bool timereference_Display=true;
        if (Chunk_Strings->Strings["description"].empty()
         && Chunk_Strings->Strings["originator"].empty()   
         && Chunk_Strings->Strings["originatorreference"].empty()   
         && Chunk_Strings->Strings["originationdate"].empty()   
         && Chunk_Strings->Strings["originationtime"].empty()   
         && Chunk_Strings->Strings["timereference"].empty()   
         && Chunk_Strings->Strings["umid"].empty()   
         && Chunk_Strings->Strings["loudnessvalue"].empty()
         && Chunk_Strings->Strings["loudnessrange"].empty()
         && Chunk_Strings->Strings["maxtruepeaklevel"].empty()
         && Chunk_Strings->Strings["maxmomentaryloudness"].empty()
         && Chunk_Strings->Strings["maxshorttermloudness"].empty()
         && Chunk_Strings->Strings["codinghistory"].empty())
            timereference_Display=false;
        ZtringList List;
        List.Separator_Set(0, __T(","));
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["description"]));
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["originator"]));
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["originatorreference"]));
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["originationdate"]));
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["originationtime"]));
        List.push_back(Chunk_Strings->Strings["timereference (translated)"].empty()?(timereference_Display?Ztring("00:00:00.000"):Ztring()):Ztring().From_UTF8(Chunk_Strings->Strings["timereference (translated)"]));
        List.push_back(Chunk_Strings->Strings["timereference"].empty()?(timereference_Display?Ztring("0"):Ztring()):Ztring().From_UTF8(Chunk_Strings->Strings["timereference"]));
        List.push_back(timereference_Display?Ztring().From_UTF8(Chunk_Strings->Strings["bextversion"]):Ztring());
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["umid"]));
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["loudnessvalue"]));
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["loudnessrange"]));
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["maxtruepeaklevel"]));
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["maxmomentaryloudness"]));
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["maxshorttermloudness"]));
        List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings["codinghistory"]));
        return List.Read().To_UTF8();
    }
    //Special cases
    if (Field=="INFO" && &Chunk_Strings && Chunk_Strings)
    {
        ZtringList List;
        List.Separator_Set(0, __T(","));
        for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Info]; Pos++)
             List.push_back(Ztring().From_UTF8(Chunk_Strings->Strings[xxxx_Strings[Fields_Info][Pos]]));
        return List.Read().To_UTF8();
    }
    if ((Field=="timereference (translated)" || Field=="timereference") && Chunk_Strings)
    {
        bool timereference_Display=false;
        for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Bext]; Pos++)
             if (!Chunk_Strings->Strings[Field_Get(xxxx_Strings[Fields_Bext][Pos])].empty() && string(xxxx_Strings[Fields_Bext][Pos])!="BextVersion")
                timereference_Display=true;
        if (Field=="timereference (translated)")
            return Chunk_Strings->Strings["timereference"].empty()?(timereference_Display?string("00:00:00.000"):string()):Ztring().Duration_From_Milliseconds((int64u)(((float64)Ztring().From_UTF8(Chunk_Strings->Strings["timereference"]).To_int64u())*1000/Chunks->Global->fmt_->sampleRate)).To_UTF8();
        else
            return Chunk_Strings->Strings["timereference"].empty()?(timereference_Display?string("0"):string()):Chunk_Strings->Strings["timereference"];
    }
    if (Field=="bextversion" && Chunk_Strings)
    {
        bool bextversion_Display=false;
        for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Bext]; Pos++)
             if (!Chunk_Strings->Strings[Field_Get(xxxx_Strings[Fields_Bext][Pos])].empty())
                bextversion_Display=true;
        return bextversion_Display?Chunk_Strings->Strings["bextversion"]:string();
    }

    if (&Chunk_Strings && Chunk_Strings && Chunk_Strings->Strings.find(Field)!=Chunk_Strings->Strings.end())
        return Chunk_Strings->Strings[Field];
    else
        return Riff_Handler_EmptyZtring_Const.To_UTF8();
}

//---------------------------------------------------------------------------
bool Riff_Handler::Set(const string &Field, const string &Value, Riff_Base::global::chunk_strings* &Chunk_Strings, int32u Chunk_Name2, int32u Chunk_Name3)
{
    if (!File_IsValid)
        return false;
    
    if ((Chunk_Strings!=NULL && Value==Chunk_Strings->Strings[Field])
     || (Chunk_Strings==NULL && Value.empty()))
        return true; //Nothing to do

    //Overwrite_Rejec
    if (Overwrite_Reject && Chunk_Strings!=NULL && !Chunk_Strings->Strings[Field].empty() && !IsModified_Internal(Field))
    {
        Errors<<(Chunks?Chunks->Global->File_Name.To_UTF8():"")<<": overwriting is not authorized ("<<Field<<")"<<endl;
        return false;
    }

    //Log
    Ztring Value_ToDisplay=Ztring().From_UTF8(Value);
    Value_ToDisplay.FindAndReplace(__T("\r"), __T(" "), 0, Ztring_Recursive);
    Value_ToDisplay.FindAndReplace(__T("\n"), __T(" "), 0, Ztring_Recursive);
    Information<<(Chunks?Chunks->Global->File_Name.To_UTF8():"")<<": "<<Field<<", "<<((Chunk_Strings==NULL || Chunk_Strings->Strings[Field].empty())?"(empty)":((Field=="xmp" || Field=="axml" || Field=="ixml")?"(XML data)":Chunk_Strings->Strings[Field].c_str()))<<" --> "<<(Value.empty()?"(removed)":((Field=="xmp" || Field=="axml" || Field=="ixml")?"(XML data)":Value_ToDisplay.To_UTF8().c_str()))<<endl;
       
    //Special cases - Before
    if (Chunk_Strings==NULL)
        Chunk_Strings=new Riff_Base::global::chunk_strings();
    if (&Chunk_Strings==&Chunks->Global->bext && Field!="bextversion")
    {
        if (Field=="umid" && !Value.empty() && Ztring().From_UTF8(Get_Internal("bextversion")).To_int16u()<1)
            Set("bextversion", "1", Chunk_Strings, Chunk_Name2, Chunk_Name3);
        if ((Field=="loudnessvalue" || Field=="loudnessrange" || Field=="maxtruepeaklevel" || Field=="maxmomentaryloudness" || Field=="maxshorttermloudness") && !Value.empty() && Ztring().From_UTF8(Get_Internal("bextversion")).To_int16u()<2)
            Set("bextversion", "2", Chunk_Strings, Chunk_Name2, Chunk_Name3);
        if (!Value.empty() && Chunk_Strings->Strings["bextversion"].empty())
            Set("bextversion", Ztring::ToZtring(Bext_DefaultVersion).To_UTF8(), Chunk_Strings, Chunk_Name2, Chunk_Name3);
    }

    //Filling
    bool Alreadyexists=false;
    for (size_t Pos=0; Pos<Chunk_Strings->Histories[Field].size(); Pos++)
        if (Chunk_Strings->Histories[Field][Pos].To_UTF8()==Chunk_Strings->Strings[Field])
            Alreadyexists=true;
    if (!Alreadyexists)
        Chunk_Strings->Histories[Field].push_back(Ztring().From_UTF8(Chunk_Strings->Strings[Field]));
    Chunk_Strings->Strings[Field]=Value;

    //Special cases - After
    if (Chunk_Strings && &Chunk_Strings==&Chunks->Global->bext && Field!="bextversion")
    {
        bool bextversion_Delete=true;
        for (size_t Pos=0; Pos<xxxx_Strings_Size[Fields_Bext]; Pos++)
             if (!Chunk_Strings->Strings[Field_Get(xxxx_Strings[Fields_Bext][Pos])].empty() && string(xxxx_Strings[Fields_Bext][Pos])!="BextVersion")
                bextversion_Delete=false;
        if (bextversion_Delete)
            Set("bextversion", string(), Chunk_Strings, Chunk_Name2, Chunk_Name3);
    }

    return true;
}

//---------------------------------------------------------------------------
bool Riff_Handler::IsOriginal(const string &Field, const string &Value, Riff_Base::global::chunk_strings* &Chunk_Strings)
{
    if (!File_IsValid || &Chunk_Strings==NULL || Chunk_Strings==NULL)
        return Value.empty();
   
    //Special cases
    if (Field=="timereference (translated)" && &Chunk_Strings && Chunk_Strings && Chunk_Strings->Strings.find("timereference")!=Chunk_Strings->Strings.end())
        return IsOriginal_Internal("timereference", Get_Internal("timereference"));

    if (Chunk_Strings->Histories[Field].empty())
        return Value==Chunk_Strings->Strings[Field];
   
    return Value==Chunk_Strings->Histories[Field][0].To_UTF8();
}

//---------------------------------------------------------------------------
bool Riff_Handler::IsModified(const string &Field, Riff_Base::global::chunk_strings* &Chunk_Strings)
{
    if (!File_IsValid)
        return false;

    //Special cases
    if (Field=="timereference (translated)" && &Chunk_Strings && Chunk_Strings && Chunk_Strings->Strings.find("timereference")!=Chunk_Strings->Strings.end())
        return IsModified_Internal("timereference");

    if (&Chunk_Strings!=NULL && Chunk_Strings && Chunk_Strings->Histories.find(Field)!=Chunk_Strings->Histories.end())
    {
        //Special cases
        if (Field=="bextversion")
            return !Chunk_Strings->Histories["bextversion"].empty() && !(Chunk_Strings->Strings["bextversion"]=="0" || Chunk_Strings->Histories["bextversion"][0].To_UTF8()==Chunk_Strings->Strings["bextversion"]);

        return !Chunk_Strings->Histories[Field].empty() && Chunk_Strings->Histories[Field][0].To_UTF8()!=Chunk_Strings->Strings[Field];
    }
    else
        return false;
}

//---------------------------------------------------------------------------
string Riff_Handler::History(const string &Field, Riff_Base::global::chunk_strings* &Chunk_Strings)
{
    //Special cases
    if (Field=="timereference (translated)" && &Chunk_Strings && Chunk_Strings && Chunk_Strings->Strings.find("timereference")!=Chunk_Strings->Strings.end() && Chunks->Global->fmt_ && Chunks->Global->fmt_->sampleRate)
    {
        ZtringList List; List.Write(Ztring().From_UTF8(History("timereference", Chunk_Strings)));
        for (size_t Pos=0; Pos<List.size(); Pos++)
            List[Pos].Duration_From_Milliseconds((int64u)(((float64)List[Pos].To_int64u())*1000/Chunks->Global->fmt_->sampleRate));
        
        //Removing Duplicate (Zero only, others are already done in timereference)
        bool ZeroAlreadyDone=false;
        for (size_t Pos=0; Pos<List.size(); Pos++)
        {
            if (List[Pos]==__T("00:00:00.000"))
            {
                if (ZeroAlreadyDone)
                {
                    List.erase(List.begin()+Pos);
                    Pos--;
                }
                else
                    ZeroAlreadyDone=true;
            }
        }
        return List.Read().To_UTF8();
    }

    if (&Chunk_Strings!=NULL && Chunk_Strings && Chunk_Strings->Strings.find(Field)!=Chunk_Strings->Strings.end())
        return Chunk_Strings->Histories[Field].Read().To_UTF8();
    else
        return Riff_Handler_EmptyZtring_Const.To_UTF8();
}

//***************************************************************************
// Configuration
//***************************************************************************

//---------------------------------------------------------------------------
void Riff_Handler::Options_Update()
{
    CriticalSectionLocker CSL(CS);

    Options_Update_Internal();
}

void Riff_Handler::Options_Update_Internal(bool Update)
{
    if (Chunks==NULL || Chunks->Global==NULL)
        return;

    Chunks->Global->NoPadding_Accept=NoPadding_Accept;
    Chunks->Global->NewChunksAtTheEnd=NewChunksAtTheEnd;
    Chunks->Global->GenerateMD5=GenerateMD5;
    Chunks->Global->VerifyMD5=VerifyMD5;
    Chunks->Global->VerifyMD5_Force=VerifyMD5_Force;
    Chunks->Global->EmbedMD5=EmbedMD5;
    Chunks->Global->EmbedMD5_AuthorizeOverWritting=EmbedMD5_AuthorizeOverWritting;
    Chunks->Global->Trace_UseDec=Trace_UseDec;

    //MD5
    if (Update && (Chunks->Global->VerifyMD5 || Chunks->Global->VerifyMD5_Force))
    {
        //Removing all MD5 related info
        Ztring PerFile_Error_Temp=Ztring().From_UTF8(PerFile_Error.str());
        PerFile_Error_Temp.FindAndReplace(Ztring("MD5, failed verification\n"), Ztring());
        PerFile_Error_Temp.FindAndReplace(Ztring("MD5, no existing MD5 chunk\n"), Ztring());
        PerFile_Error.str(PerFile_Error_Temp.To_UTF8());
        Ztring PerFile_Information_Temp=Ztring().From_UTF8(PerFile_Information.str());
        PerFile_Information_Temp.FindAndReplace(Ztring("MD5, no existing MD5 chunk\n"), Ztring());
        PerFile_Information_Temp.FindAndReplace(Ztring("MD5, verified\n"), Ztring());
        PerFile_Information.str(PerFile_Information_Temp.To_UTF8());
        
        //Checking
        if (!(Chunks->Global->MD5Stored && !Chunks->Global->MD5Stored->Strings["md5stored"].empty()))
        {
                if (Chunks->Global->VerifyMD5_Force)
                {
                    Errors<<Chunks->Global->File_Name.To_UTF8()<<": MD5, no existing MD5 chunk"<<endl;
                    PerFile_Error.str(string());
                    PerFile_Error<<"MD5, no existing MD5 chunk"<<endl;
                }
                else
                {
                    Information<<Chunks->Global->File_Name.To_UTF8()<<": MD5, no existing MD5 chunk"<<endl;
                    PerFile_Information<<"MD5, no existing MD5 chunk"<<endl;
                }
        }
        else if (Chunks->Global->MD5Generated && Chunks->Global->MD5Generated->Strings["md5generated"]!=Chunks->Global->MD5Stored->Strings["md5stored"])
        {
            Errors<<Chunks->Global->File_Name.To_UTF8()<<": MD5, failed verification"<<endl;
            PerFile_Error.str(string());
            PerFile_Error<<"MD5, failed verification"<<endl;
        }
        else
        {
            Information<<Chunks->Global->File_Name.To_UTF8()<<": MD5, verified"<<endl;
            PerFile_Information.str(string());
            PerFile_Information<<"MD5, verified"<<endl;
        }
    }
    if (EmbedMD5
     && Chunks->Global->MD5Generated && !Chunks->Global->MD5Generated->Strings["md5generated"].empty()
     && (!(Chunks->Global->MD5Stored && !Chunks->Global->MD5Stored->Strings["md5stored"].empty())
      || EmbedMD5_AuthorizeOverWritting))
            Set_Internal("MD5Stored", Chunks->Global->MD5Generated->Strings["md5generated"], rules());
}

//***************************************************************************
// Helpers - Cues
//***************************************************************************

//---------------------------------------------------------------------------
string Riff_Handler::Cue_Xml_Get()
{
    ZtringListList Points(Ztring().From_UTF8(Get_Internal("cue").c_str()));
    ZtringListList Labels=(Ztring().From_UTF8(Get_Internal("labl").c_str()));
    ZtringListList Notes=(Ztring().From_UTF8(Get_Internal("note").c_str()));
    ZtringListList Texts=(Ztring().From_UTF8(Get_Internal("ltxt").c_str()));

    if (Points.empty())
        return string();

    tinyxml2::XMLPrinter Printer;
    Printer.PushHeader(false, true);

    Printer.OpenElement("Cues");
    for (size_t Pos=0; Pos<Points.size(); Pos++)
    {
        Ztring Id=Points[Pos](0);

        Printer.OpenElement("Cue");
            Printer.OpenElement("ID");
                Printer.PushText(Id.To_UTF8().c_str());
            Printer.CloseElement();
            Printer.OpenElement("Position");
                Printer.PushText(Points[Pos](1).To_UTF8().c_str());
            Printer.CloseElement();
            Printer.OpenElement("DataChunkID");
                Printer.PushText(Points[Pos](2).To_UTF8().c_str());
            Printer.CloseElement();
            Printer.OpenElement("ChunkStart");
                Printer.PushText(Points[Pos](3).To_UTF8().c_str());
            Printer.CloseElement();
            Printer.OpenElement("BlockStart");
                Printer.PushText(Points[Pos](4).To_UTF8().c_str());
            Printer.CloseElement();
            Printer.OpenElement("SampleOffset");
                Printer.PushText(Points[Pos](5).To_UTF8().c_str());
            Printer.CloseElement();
        for (size_t Label_Pos=Labels.Find(Id, 0, 0); Label_Pos!=Error; Label_Pos=Labels.Find(Id, 0, 0))
        {
            Printer.OpenElement("Label");
                Ztring Label=Labels[Label_Pos](1);
                Label.FindAndReplace(__T("\\n"), __T("\n"), 0, Ztring_Recursive);
                Printer.PushText(Label.To_UTF8().c_str());
            Printer.CloseElement();
            Labels.erase(Labels.begin()+Label_Pos);
        }
        for (size_t Note_Pos=Notes.Find(Id, 0, 0); Note_Pos!=Error; Note_Pos=Notes.Find(Id, 0, 0))
        {
            Printer.OpenElement("Note");
                Ztring Note=Notes[Note_Pos](1);
                Note.FindAndReplace(__T("\\n"), __T("\n"), 0, Ztring_Recursive);
                Printer.PushText(Note.To_UTF8().c_str());
            Printer.CloseElement();
            Notes.erase(Notes.begin()+Note_Pos);
        }
        for (size_t Text_Pos=Texts.Find(Id, 0, 0); Text_Pos!=Error; Text_Pos=Texts.Find(Id, 0, 0))
        {
            Printer.OpenElement("LabeledText");
                Printer.OpenElement("SampleLength");
                    Printer.PushText(Texts[Text_Pos](1).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("PurposeID");
                    Printer.PushText(Texts[Text_Pos](2).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("Country");
                    Printer.PushText(Texts[Text_Pos](3).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("Language");
                    Printer.PushText(Texts[Text_Pos](4).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("Dialect");
                    Printer.PushText(Texts[Text_Pos](5).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("CodePage");
                    Printer.PushText(Texts[Text_Pos](6).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("Text");
                    Ztring Text=Texts[Text_Pos](7);
                    Text.FindAndReplace(__T("\\n"), __T("\n"), 0, Ztring_Recursive);
                    Printer.PushText(Text.To_UTF8().c_str());
                Printer.CloseElement();
            Printer.CloseElement();
            Texts.erase(Texts.begin()+Text_Pos);
        }
        Printer.CloseElement();
    }

    for (size_t Label_Pos=0; Label_Pos<Labels.size(); Label_Pos++)
    {
        Printer.OpenElement("Cue");
            Printer.OpenElement("ID");
                Printer.PushAttribute("Missing", "true");
                Printer.PushText(Labels[Label_Pos](0).To_UTF8().c_str());
            Printer.CloseElement();
            Printer.OpenElement("Label");
                Ztring Label=Labels[Label_Pos](1);
                Label.FindAndReplace(__T("\\n"), __T("\n"), 0, Ztring_Recursive);
                Printer.PushText(Label.To_UTF8().c_str());
            Printer.CloseElement();
        Printer.CloseElement();
    }

    for (size_t Note_Pos=0; Note_Pos<Notes.size(); Note_Pos++)
    {
        Printer.OpenElement("Cue");
            Printer.OpenElement("ID");
                Printer.PushAttribute("Missing", "true");
                Printer.PushText(Notes[Note_Pos](0).To_UTF8().c_str());
            Printer.CloseElement();
            Printer.OpenElement("Note");
                Ztring Note=Notes[Note_Pos](1);
                Note.FindAndReplace(__T("\\n"), __T("\n"), 0, Ztring_Recursive);
                Printer.PushText(Note.To_UTF8().c_str());
            Printer.CloseElement();
        Printer.CloseElement();
    }

    for (size_t Text_Pos=0; Text_Pos<Texts.size(); Text_Pos++)
    {
        Printer.OpenElement("Cue");
            Printer.OpenElement("ID");
                Printer.PushAttribute("Missing", "true");
                Printer.PushText(Texts[Text_Pos](0).To_UTF8().c_str());
            Printer.CloseElement();
            Printer.OpenElement("LabeledText");
                Printer.OpenElement("SampleLength");
                    Printer.PushText(Texts[Text_Pos](1).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("PurposeID");
                    Printer.PushText(Texts[Text_Pos](2).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("Country");
                    Printer.PushText(Texts[Text_Pos](3).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("Language");
                    Printer.PushText(Texts[Text_Pos](4).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("Dialect");
                    Printer.PushText(Texts[Text_Pos](5).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("CodePage");
                    Printer.PushText(Texts[Text_Pos](6).To_UTF8().c_str());
                Printer.CloseElement();
                Printer.OpenElement("Text");
                    Ztring Text=Texts[Text_Pos](7);
                    Text.FindAndReplace(__T("\\n"), __T("\n"), 0, Ztring_Recursive);
                    Printer.PushText(Text.To_UTF8().c_str());
                Printer.CloseElement();
            Printer.CloseElement();
        Printer.CloseElement();
    }

    Printer.CloseElement();

    return string(Printer.CStr());
}

//---------------------------------------------------------------------------
bool Riff_Handler::Cue_Xml_Set (const string& Xml, rules Rules)
{
    string Points;
    string Labels;
    string Notes;
    string Texts;

    if(!Cue_Xml_To_Fields(Xml, Points, Labels, Notes, Texts))
        return false;

    Set_Internal("cue", Points, Rules);
    Set_Internal("labl", Labels, Rules);
    Set_Internal("note", Notes, Rules);
    Set_Internal("ltxt", Texts, Rules);

    return true;
}

bool Riff_Handler::Cue_Xml_To_Fields (const string& Xml, string& cue_, string& labl, string& note, string& ltxt)
{
    cue_ = string();
    labl = string();
    note = string();
    ltxt = string();

    if (Xml.empty())
        return true;

    ZtringListList Points;
    ZtringListList Labels;
    ZtringListList Notes;
    ZtringListList Texts;

    tinyxml2::XMLDocument Document;
    if (Document.Parse(Xml.c_str())!=tinyxml2::XML_SUCCESS)
        return false;

    tinyxml2::XMLElement* Root=Document.FirstChildElement("Cues")->ToElement();

    if (!Root)
        return false;

    for(tinyxml2::XMLNode* Node=Root->FirstChildElement("Cue"); Node; Node=Node->NextSibling())
    {
        ZtringList Point, Label, Note, Text;
        Ztring Id;

        tinyxml2::XMLElement* Element=NULL;

        Element=Node->FirstChildElement("ID");
        if (!Element)
            continue;

        Id=Ztring().From_Number(Element->UnsignedText(0));
        if (!Element->BoolAttribute("Missing"))
        {
            Point.push_back(Id);
            Element=Node->FirstChildElement("Position");
            Point.push_back(Ztring().From_Number(Element?Element->UnsignedText(0):0));
            Element=Node->FirstChildElement("DataChunkID");
            Point.push_back(Ztring().From_UTF8(Element&&Element->GetText()?Element->GetText():"0x64617461"));
            Element=Node->FirstChildElement("ChunkStart");
            Point.push_back(Ztring().From_Number(Element?Element->UnsignedText(0):0));
            Element=Node->FirstChildElement("BlockStart");
            Point.push_back(Ztring().From_Number(Element?Element->UnsignedText(0):0));
            Element=Node->FirstChildElement("SampleOffset");
            Point.push_back(Ztring().From_Number(Element?Element->UnsignedText(0):0));
            Points.push_back(Point);
        }
        for (Element=Node->FirstChildElement("Label"); Element; Element=Element->NextSiblingElement("Label"))
        {
            Ztring Value=Ztring().From_UTF8(Element->GetText()?Element->GetText():"");
            Value.FindAndReplace(__T("\r\n"), __T("\n"), 0, Ztring_Recursive);
            Value.FindAndReplace(__T("\n"), __T("\\n"), 0, Ztring_Recursive);
            Label.push_back(Id);
            Label.push_back(Value);
            Labels.push_back(Label);
        }
        for (Element=Node->FirstChildElement("Note"); Element; Element=Element->NextSiblingElement("Note"))
        {
            Ztring Value=Ztring().From_UTF8(Element->GetText()?Element->GetText():"");
            Value.FindAndReplace(__T("\r\n"), __T("\n"), 0, Ztring_Recursive);
            Value.FindAndReplace(__T("\n"), __T("\\n"), 0, Ztring_Recursive);
            Note.push_back(Id);
            Note.push_back(Value);
            Notes.push_back(Note);
        }
        for (tinyxml2::XMLElement* LabeledTextElement=Node->FirstChildElement("LabeledText"); LabeledTextElement; LabeledTextElement=LabeledTextElement->NextSiblingElement("LabeledText"))
        {
            Text.push_back(Id);
            Element=LabeledTextElement->FirstChildElement("SampleLength");
            Text.push_back(Ztring().From_Number(Element?Element->UnsignedText(0):0));
            Element=LabeledTextElement->FirstChildElement("PurposeID");
            Text.push_back(Ztring().From_UTF8(Element&&Element->GetText()?Element->GetText():"0x00000000"));
            Element=LabeledTextElement->FirstChildElement("Country");
            Text.push_back(Ztring().From_Number(Element?Element->UnsignedText(0):0));
            Element=LabeledTextElement->FirstChildElement("Language");
            Text.push_back(Ztring().From_Number(Element?Element->UnsignedText(0):0));
            Element=LabeledTextElement->FirstChildElement("Dialect");
            Text.push_back(Ztring().From_Number(Element?Element->UnsignedText(0):0));
            Element=LabeledTextElement->FirstChildElement("CodePage");
            Text.push_back(Ztring().From_Number(Element?Element->UnsignedText(0):0));
            Element=LabeledTextElement->FirstChildElement("Text");
            Ztring Value=Ztring().From_UTF8(Element&&Element->GetText()?Element->GetText():"");
            Value.FindAndReplace(__T("\r\n"), __T("\n"), 0, Ztring_Recursive);
            Value.FindAndReplace(__T("\n"), __T("\\n"), 0, Ztring_Recursive);
            Text.push_back(Value);
            Texts.push_back(Text);
        }
    }

    cue_ = Points.Read().To_UTF8();
    labl = Labels.Read().To_UTF8();
    note = Notes.Read().To_UTF8();
    ltxt = Texts.Read().To_UTF8();

    return true;
}

//***************************************************************************
// Helpers - Retrieval of chunks info
//***************************************************************************

//---------------------------------------------------------------------------
Riff_Base::global::chunk_strings** Riff_Handler::chunk_strings_Get(const string &Field)
{
    if (Chunks==NULL || Chunks->Global==NULL)
        return NULL;    
        
    string Field_Lowered=Ztring().From_UTF8(Field).MakeLowerCase().To_UTF8();
    if (Field_Lowered=="history")
        Field_Lowered="codinghistory";

    //bext
    if (Field_Lowered=="bext"
     || Field_Lowered=="description"
     || Field_Lowered=="originator"
     || Field_Lowered=="originatorreference"
     || Field_Lowered=="originationdate"
     || Field_Lowered=="originationtime"
     || Field_Lowered=="timereference (translated)"
     || Field_Lowered=="timereference"
     || Field_Lowered=="bextversion"
     || Field_Lowered=="umid"
     || Field_Lowered=="loudnessvalue"
     || Field_Lowered=="loudnessrange"
     || Field_Lowered=="maxtruepeaklevel"
     || Field_Lowered=="maxmomentaryloudness"
     || Field_Lowered=="maxshorttermloudness"
     || Field_Lowered=="codinghistory")
        return &Chunks->Global->bext;

    //MXP
    else if (Field_Lowered=="xmp")
        return &Chunks->Global->XMP;

    //aXML
    else if (Field_Lowered=="axml")
        return &Chunks->Global->aXML;
    
    //iXML
    else if (Field_Lowered=="ixml")
        return &Chunks->Global->iXML;

    //cue
    else if (Field_Lowered=="cue")
        return &Chunks->Global->cue_;

    //MD5Stored
    else if (Field_Lowered=="md5stored")
        return &Chunks->Global->MD5Stored;
    
    //MD5Stored
    else if (Field_Lowered=="md5generated")
        return &Chunks->Global->MD5Generated;

    //adtl
    else if (Field_Lowered=="labl"
          || Field_Lowered=="note"
          || Field_Lowered=="ltxt")
        return &Chunks->Global->adtl;

    //INFO
    else if (Field.size()==4)
        return &Chunks->Global->INFO;
    
    //Unknown
    else
        return NULL;
}

//---------------------------------------------------------------------------
string Riff_Handler::Field_Get(const string &Field)
{
    string Field_Lowered=Ztring().From_UTF8(Field).MakeLowerCase().To_UTF8();
    if (Field_Lowered=="history")
        Field_Lowered="codinghistory";

    if (Field_Lowered=="filename"
     || Field_Lowered=="bext"
     || Field_Lowered=="description"
     || Field_Lowered=="originator"
     || Field_Lowered=="originatorreference"
     || Field_Lowered=="originationdate"
     || Field_Lowered=="originationtime"
     || Field_Lowered=="timereference (translated)"
     || Field_Lowered=="timereference"
     || Field_Lowered=="bextversion"
     || Field_Lowered=="umid"
     || Field_Lowered=="loudnessvalue"
     || Field_Lowered=="loudnessrange"
     || Field_Lowered=="maxtruepeaklevel"
     || Field_Lowered=="maxmomentaryloudness"
     || Field_Lowered=="maxshorttermloudness"
     || Field_Lowered=="codinghistory"
     || Field_Lowered=="xmp"
     || Field_Lowered=="axml"
     || Field_Lowered=="ixml"
     || Field_Lowered=="cue"
     || Field_Lowered=="labl"
     || Field_Lowered=="note"
     || Field_Lowered=="ltxt"
     || Field_Lowered=="md5stored"
     || Field_Lowered=="md5generated")
        return Field_Lowered; 

    //Unknown 4 chars --> In INFO chunk, in uppercase
    if (Field.size()==4)
        return Ztring().From_UTF8(Field).MakeUpperCase().To_UTF8();

    //Unknown --> In INFO chunk, in uppercase
    return Field_Lowered;
}

//---------------------------------------------------------------------------
int32u Riff_Handler::Chunk_Name2_Get(const string &Field)
{
    string Field_Lowered=Ztring().From_UTF8(Field).MakeLowerCase().To_UTF8();
    if (Field_Lowered=="history")
        Field_Lowered="codinghistory";
        
    //bext
    if (Field_Lowered=="bext"
     || Field_Lowered=="description"
     || Field_Lowered=="originator"
     || Field_Lowered=="originatorreference"
     || Field_Lowered=="originationdate"
     || Field_Lowered=="originationtime"
     || Field_Lowered=="timereference (translated)"
     || Field_Lowered=="timereference"
     || Field_Lowered=="bextversion"
     || Field_Lowered=="umid"
     || Field_Lowered=="loudnessvalue"
     || Field_Lowered=="loudnessrange"
     || Field_Lowered=="maxtruepeaklevel"
     || Field_Lowered=="maxmomentaryloudness"
     || Field_Lowered=="maxshorttermloudness"
     || Field_Lowered=="codinghistory")
        return Elements::WAVE_bext; 

    //XMP
    else if (Field_Lowered=="xmp")
        return Elements::WAVE__PMX; 

    //aXML
    else if (Field_Lowered=="axml")
        return Elements::WAVE_axml; 

    //iXML
    else if (Field_Lowered=="ixml")
        return Elements::WAVE_iXML;

    //cue
    else if (Field_Lowered=="cue")
        return Elements::WAVE_cue_;

    //MD5Stored
    else if (Field_Lowered=="md5stored")
        return Elements::WAVE_MD5_; 

    //MD5Generated
    else if (Field_Lowered=="md5generated")
        return Elements::WAVE_MD5_; 

    else if (Field_Lowered=="labl"
          || Field_Lowered=="note"
          || Field_Lowered=="ltxt")
        return Elements::WAVE_adtl;

    //INFO
    else if (Field.size()==4)
        return Elements::WAVE_INFO;
    
    //Unknown
    return 0x00000000;
}

//---------------------------------------------------------------------------
int32u Riff_Handler::Chunk_Name3_Get(const string &Field)
{
    //INFO
    if (Chunk_Name2_Get(Field)==Elements::WAVE_INFO)
        return Ztring().From_UTF8(Field).MakeUpperCase().To_CC4();

    if (Chunk_Name2_Get(Field)==Elements::WAVE_adtl)
        return Ztring().From_UTF8(Field).MakeLowerCase().To_CC4();

    //Unknown / not needed
    return 0x00000000;
}
