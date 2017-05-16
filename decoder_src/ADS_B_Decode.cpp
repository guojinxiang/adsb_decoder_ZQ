#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define  ADSB_DECODER_NO_LOCK
#include "ADS_B_Decode.h"
#include <map>
#include <math.h>
#include <cassert>
#include "cpr.h"
#include "ads_altitude.h"

#ifndef ADSB_DECODER_NO_LOCK
typedef boost::shared_mutex rwmutex ; 
typedef boost::unique_lock<boost::shared_mutex> wLock ; 
typedef boost::shared_lock<boost::shared_mutex> rLock ; 

#define Write_Lock(p) wLock wl(*p) ; 
#define Read_Lock(p)  rLock rl(*p) ; 

#else

typedef void rwmutex ; 
#define Write_Lock(p)	(p) ; 
#define Read_Lock(p)  (p) ; 
#endif

typedef std::map<ICAOAddress,AircraftInfo>  DBType ; 
typedef DBType* LPDBType ; 	
typedef DBType::iterator DBPointer ;
typedef DBType::const_iterator ConstDBPointer ;

#define H2P(n) static_cast<LPDBType>(n)


typedef  struct decoder_contex
{
	rwmutex* mutex ;		//��д������
	cpr_decoder* decoder ;  //CPR������
}decoder_contex ;

std::map<DE_HANDLE,decoder_contex> DecoderContex ; 



ads_cpr_decode_param get_ads_cpr_param(BYTE ads_msg[14])
{
	ads_cpr_decode_param param ; 

	DWORD XZ_Value ;
	BYTE * pXZ_Data = &ads_msg[8];
	BYTE * pXZ_Value = (BYTE*)&XZ_Value;
	pXZ_Value[3] = pXZ_Data[0];
	pXZ_Value[2] = pXZ_Data[1];
	pXZ_Value[1] = pXZ_Data[2];
	pXZ_Value[0] = pXZ_Data[3];


	DWORD YZ_Value ;
	BYTE * pYZ_Data = &ads_msg[6];
	BYTE * pYZ_Value = (BYTE*)&YZ_Value;
	pYZ_Value[3] = pYZ_Data[0];
	pYZ_Value[2] = pYZ_Data[1];
	pYZ_Value[1] = pYZ_Data[2];
	pYZ_Value[0] = pYZ_Data[3];

	param.yz =(YZ_Value>>9)&0x0001FFFF;	//YZ(γ��Bin)
	param.xz = (XZ_Value>>8)&0x0001FFFF; //XZ(����Bin)
	param.cpr_type = (ads_msg[6]>>2)&0x01; //��ż��־

	return param ; 

}

void SetDecoderHome(DE_HANDLE deHandle ,double lon , double lat)
{
	std::map<DE_HANDLE,decoder_contex> ::iterator it = DecoderContex.find(deHandle) ;
	if(it==DecoderContex.end())
		return  ;
	else 
	{
		cpr_loc_t loc = {lon , lat } ; 
		it->second.decoder->set_location(loc) ;
	}
}



DE_HANDLE InitNewDecoder(double home_lon , double home_lat)
{
	DBType* pNewInstance = new DBType ;
	assert(pNewInstance);

	if(pNewInstance==NULL)
		return INVALID_DECODE_HANDLE ; 

	decoder_contex contex ; 

	cpr_loc_t home_location = {home_lon , home_lat} ; 

#ifdef ADSB_DECODER_NO_LOCK
	contex.mutex  = NULL ; 
#else
	contex.mutex = new rwmutex ; 
#endif

	contex.decoder = new cpr_decoder(home_location);
	assert(contex.decoder);

	if( contex.decoder==NULL)
		return INVALID_DECODE_HANDLE ; 

	DecoderContex.insert(std::pair<DE_HANDLE,decoder_contex>( static_cast<DE_HANDLE>(pNewInstance) , contex)) ; 
	return static_cast<DE_HANDLE>(pNewInstance);
}


cpr_decoder* get_decoder(DE_HANDLE deHandle) 
{
	std::map<DE_HANDLE,decoder_contex> ::iterator it = DecoderContex.find(deHandle) ;
	if(it==DecoderContex.end())
		return NULL ;
	else 
		return it->second.decoder; 
}

rwmutex* GetRWMutex(DE_HANDLE deHandle)
{
#ifdef ADSB_DECODER_NO_LOCK
	return NULL ; 
#else
	std::map<DE_HANDLE,decoder_contex> ::iterator it = DecoderContex.find(deHandle) ;
	if(it==DecoderContex.end())
		return NULL ;
	else 
		return it->second.mutex; 
#endif

}

BOOL IsExistICAO(DE_HANDLE deHandle , ICAOAddress ICAO)
{
	LPDBType pDB = H2P(deHandle) ; 

	ConstDBPointer  p = pDB->find(ICAO);
	return (p!=pDB->end())?TRUE:FALSE ; 
}


//��ʼ�����ݿ���Ŀ
void InitAircraftEntry(DE_HANDLE deHandle ,ICAOAddress ICAO , ADS_Time Time , AircraftInfo* pInfo)
{
	pInfo->ICAOID = ICAO ; 

	//��IdentificationInfo�ṹ���г�ʼ��
	memset(pInfo->IdAndCategory.FlightID,0,8) ; 
	memset(pInfo->IdAndCategory.OriFlightID , 0 ,6);
	pInfo->IdAndCategory.CategorySet = CategorySetA ; 
	pInfo->IdAndCategory.Category = 0 ; 

	//��AircraftLocationInfo�ṹ���г�ʼ��
	pInfo->Location.Altitude     = 0 ;
	pInfo->Location.EvalLon      = 0 ; 
	pInfo->Location.EvalLat      = 0 ;
	pInfo->Location.LocType      = Air ; 
	pInfo->Location.LocPercision.NACP = UNKNOWN_NACP ; 
	pInfo->Location.LocPercision.PercisionVer = VERSION_ZERO ; 
	pInfo->Location.LocPercision.NIC = NULL_PERCISION ; 
	pInfo->Location.LocPercision.NUC = NULL_PERCISION ; 
	pInfo->Location.SurveillanceStatus = 0 ;
	pInfo->Location.T = INVALID_T ; 
	pInfo->Location.Q = INVALID_Q ; 
	pInfo->Location.LocationAvailable = FALSE ; 

	//��SurfacePosInfo�ṹ���г�ʼ��
	pInfo->SurfaceState.MoveMentCode = 0 ;
	pInfo->SurfaceState.Movement = 0 ;
	pInfo->SurfaceState.HeadingState = 0 ;
	pInfo->SurfaceState.Heading=  0 ; 

	//��VelocityInfo�ṹ���г�ʼ��
	pInfo->VelocityState.GSInfoAvailable = FALSE ; 
	pInfo->VelocityState.ASInfoAvailable = FALSE ; 

	//�����������ʱ��
	pInfo->LocationLastUpdateTime = Time ;

	pInfo->bTimeOut = FALSE ; 

}
//�����µ���Ŀ
void AddEntry(DE_HANDLE deHandle , ICAOAddress ICAO ,const AircraftInfo& Info)
{
	LPDBType pDB = H2P(deHandle) ; 
	pDB->insert(std::pair<ICAOAddress,AircraftInfo>(ICAO , Info)) ; 
}


//����ָ��ICAOĿ�����Ϣ
AircraftInfo* GetAircraftInfo(DE_HANDLE deHandle , ICAOAddress ICAO)
{
	LPDBType pDB = H2P(deHandle) ; 

	rwmutex* mutex = GetRWMutex(deHandle)  ;
	Read_Lock(mutex) ; //������

	DBPointer  p = pDB->find(ICAO);
	if(p==pDB->end()) return NULL ; 
	return &(p->second) ; 
}


//���ص�ǰ����Ŀ�������
size_t GetTotalCount(DE_HANDLE deHandle) 
{
	LPDBType pDB = H2P(deHandle) ; 

	rwmutex* mutex = GetRWMutex(deHandle)  ;
	Read_Lock(mutex) ; //������
	return pDB->size() ; 
}

/////////////////////////////////////���ߺ���/////////////////////////////////////���صķָ���
time_t  MakeADSTime(ADS_Time ads_time)
{
	time_t t_of_day=mktime(&ads_time.t);
	return t_of_day ;  
}

//////////////////////////����λ����ؽ���////////////////////////////////////////////////���صķָ���

//����ȫ�ֽ���ʱ������Ϣ����ʱ����(10��)
#define VALID_POSITION_UPDATE_INTERVAL 60

typedef unsigned long ULONG_PTR, *PULONG_PTR;
typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR;
#ifndef MAKEWORD
#define MAKEWORD(a, b)      ((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | ((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8))
#endif

//��VER = 0 ʱ��ȡ��NUC
DWORD GetNUC(const BYTE* Data)
{
	BYTE Code_Type = 	(Data[4]>>3)&0x1F; //��Ϣ����
	DWORD a = (DWORD)Code_Type ; 
	return DWORD(18-a) ; 
}


//��VER = 1 ʱ��ȡ��NIC
DWORD GetNIC(const  BYTE* Data ,BYTE NIC_SUPPLEMENT_A)
{
	static DWORD onzero[10] = {11,10,8,7,6,5,4,2,1,0} ; 
	static DWORD onone[10] =  {11,10,9,7,6,5,4,3,1,0} ; 
	BYTE Code_Type = 	(Data[4]>>3)&0x1F; //��Ϣ����

	if(NIC_SUPPLEMENT_A==0)
	{		return onzero[Code_Type-9] ; 
	}
	else
	{
		return onone[Code_Type-9] ; 
	}
}


const double  AirDlat0   = 6.0;  
const double  AirDlat1   = 360.0 / 59.0;
const double  SurfDlat0  = 1.5;
const double  SurfDlat1 = 90.0 / 59.0;




bool float_val_equ(double a ,double b)
{
	const double min_flt= 1e-3 ;
	return fabs(a-b)<min_flt;

}

BOOL DecodeAirPostion(DE_HANDLE deHandle ,  // [IN] ���������
	BYTE AdsMessage[14], // [IN] ADS��Ϣ����
	ADS_Time Time       // [IN] ���Ľ���ʱ��
	) 
{
	rwmutex* mutex = GetRWMutex(deHandle)  ;
	Write_Lock(mutex) ; //д����

	BOOL bHavePos = FALSE ; 
	LPDBType pDB = H2P(deHandle) ; 
	ICAOAddress  ICAO = ICAOAddress(AdsMessage[1] ,AdsMessage[2] ,AdsMessage[3]) ;

	if(!IsExistICAO(deHandle , ICAO)) //���ݿ��в������˷�������Ŀ
	{
		AircraftInfo Info ; 
		InitAircraftEntry(deHandle , ICAO,Time,&Info) ; 
		AddEntry(deHandle , ICAO ,Info ) ;
	}

	DBPointer IT = pDB->find(ICAO) ; 
	AircraftInfo* InfoPointer = &(IT->second) ; 

	InfoPointer->InfoLastUpdateTime = Time ;  //�������¸���ʱ��
	if(InfoPointer->bTimeOut)
	{
		InfoPointer->bTimeOut = FALSE ; 
	}

	BYTE Code_Type = 	(AdsMessage[4]>>3)&0x1F; //DF17��Ϣ����


	if(  (Code_Type>=9 && Code_Type<=18 ) /*||( Code_Type>=20 &&Code_Type<=22)*/  ) //�������о�γ����Ϣ
	{

		BYTE CPRType = (AdsMessage[6]>>2)&0x01; //��ż��־
		DWORD Ver = InfoPointer->Location.LocPercision.PercisionVer ; 
		//����ʱ��ͬ����־λT
		InfoPointer->Location.T =  (AdsMessage[6]>>3)&0x01 ; 

		if(Code_Type>=9 && Code_Type<=18 )		//���;��ȼ���
		{
			InfoPointer->Location.AltType = Baro ; 

			InfoPointer->Location.LocPercision.NIC_SUPPLEMENT_B = AdsMessage[4]&0x01 ;  //NIC_Supplement_B
			if(Ver == VERSION_ONE)
			{
				DWORD NIC = GetNIC(AdsMessage ,InfoPointer->Location.LocPercision.NIC_SUPPLEMENT_A) ; 
				InfoPointer->Location.LocPercision.NIC = NIC ; 
			}
			else if(Ver == VERSION_ZERO)
			{
				DWORD NUC = GetNUC(AdsMessage) ; 
				InfoPointer->Location.LocPercision.NUC = NUC ; 
			}		

		}else
		{
			InfoPointer->Location.AltType = HAE	 ; 
			if(Ver == VERSION_ONE)
			{
				if(Code_Type==20){InfoPointer->Location.LocPercision.NIC = 11 ; }
				else if(Code_Type==21){InfoPointer->Location.LocPercision.NIC = 10 ; }
				else {InfoPointer->Location.LocPercision.NIC = 0 ; }
			}
			else if(Ver == VERSION_ZERO)
			{
				if(Code_Type==20){InfoPointer->Location.LocPercision.NUC = 9 ; }
				else if(Code_Type==21){InfoPointer->Location.LocPercision.NUC =8 ; }
				else {InfoPointer->Location.LocPercision.NUC = 0 ; }
			}		

		}

		InfoPointer->Location.LocType = Air ;  //����Ŀ��
		//����߶���Ϣ
		unsigned short  alt_word = get_ads_b_alt_word(AdsMessage) ;
		int real_alt =  decode_alt(alt_word , 0) ; 
		InfoPointer->Location.Altitude = real_alt ;


		//����λ����Ϣ
		cpr_decoder* decoder = get_decoder(deHandle);
		assert(decoder);

		ads_cpr_decode_param cpr_param  = get_ads_cpr_param(AdsMessage) ;

		cpr_loc_t loc =   decoder->decode((DWORD)ICAO ,cpr_param.yz ,cpr_param.xz ,cpr_param.cpr_type ,0) ; 
		if(!valid_location(loc))
		{
			bHavePos = FALSE ;
			InfoPointer->Location.LocationAvailable = FALSE  ;
		}
		else
		{
			//���֮ǰ��λ��ʱ���õģ�����Ե�ǰ�����λ�ã���֮ǰ�����λ�õĲ�ֵ�Ƿ�ܴ�
			if(InfoPointer->Location.LocationAvailable) 
			{
				if( (fabs(InfoPointer->Location.EvalLon  - loc.lon  )>2 )   \
					|| (fabs(InfoPointer->Location.EvalLat -  loc.lat) >2) )
				{


					bHavePos = FALSE ;
					InfoPointer->Location.LocationAvailable = FALSE  ;

					decoder->reset((DWORD)ICAO) ;
				}
				else
				{
					InfoPointer->Location.EvalLon =  loc.lon;
					InfoPointer->Location.EvalLat = loc.lat;

					bHavePos = TRUE ;
					InfoPointer->Location.LocationAvailable = TRUE  ;

				}
			}
			else
			{

				InfoPointer->Location.EvalLon =  loc.lon;
				InfoPointer->Location.EvalLat = loc.lat;

				bHavePos = TRUE ;
				InfoPointer->Location.LocationAvailable = TRUE  ;
			}
		}
	}
	InfoPointer->LocationLastUpdateTime =  Time;
	return bHavePos ;

}

/////////////////////////////////�ٶ������Ϣ����/////////////////////////////////////////���صķָ���
#define PI 3.1415926
const double DEGREE = 180.0/PI;
//��������ٶȵĺ���
double TrueTrack(int Velocityn_s,int Velocitye_w,int Signlat,int signlon)
{
	double Track = 0.0;
	if(Velocityn_s==-1 && Velocitye_w==-1) return 0 ;

	if(Velocityn_s==0)
	{
		if(signlon==0)
			return 90.0;
		else
			return 270.0;

	}

	Track = DEGREE*atan((double)Velocitye_w/((double)Velocityn_s));
	if(Signlat==1)
	{
		if(signlon==0)
			Track = 180-Track;
		else
			Track = 180+Track;
	}
	else
	{

		if(signlon==1)
			Track = 360 - Track;
	}

	return Track ;

}
BOOL DecodeVelocity(DE_HANDLE deHandle ,     // [IN] ���������
	BYTE AdsMessage[14],    // [IN] ADS��Ϣ����
	ADS_Time Time          // [IN] ���Ľ���ʱ��
	)
{

	rwmutex* mutex = GetRWMutex(deHandle)  ;
	Write_Lock(mutex) ; //д����

	LPDBType pDB = H2P(deHandle) ; 

	ICAOAddress  ICAO = ICAOAddress(AdsMessage[1] ,AdsMessage[2] ,AdsMessage[3]) ;
	if(!IsExistICAO(deHandle , ICAO)) //���ݿ��в������˷�������Ŀ
	{
		AircraftInfo Info ; 
		InitAircraftEntry(deHandle , ICAO,Time,&Info) ; 
		AddEntry(deHandle , ICAO ,Info ) ;
	}
	DBPointer IT = pDB->find(ICAO) ; 
	AircraftInfo* InfoPointer = &(IT->second) ; 


	InfoPointer->InfoLastUpdateTime = Time ;  //�������¸���ʱ��
	if(InfoPointer->bTimeOut)
	{
		InfoPointer->bTimeOut = FALSE ; 
	}

	BYTE Code_Type = 	(AdsMessage[4]>>3)&0x1F; //��Ϣ����	
	if(Code_Type!=19) return FALSE ; //��Ϣ���Ͳ���19�ģ������д���


	BYTE Payload[8];
	memset(Payload,0,8);
	memcpy(&Payload[1],&AdsMessage[4],7); 
	InfoPointer->VelocityState.Velocitye_w = -1; //���ó�ʼֵ
	InfoPointer->VelocityState.Velocityn_s = -1;
	InfoPointer->VelocityState.ASHeading = -1 ;

	BYTE SubType  = Payload[1]&0x07 ;//���������� 
	InfoPointer->VelocityState.SubType = SubType;  
	InfoPointer->VelocityState.Supersonic = ((Payload[1]&0x01)^0x01)?true:false; //�Ƿ�Ϊ���������͵ķ�����
	InfoPointer->VelocityState.IntentChangeFlag = (Payload[2]&0x80)>>7; //Intent Change Flag (0--��־û�иı� 1--��־�ı�)
	InfoPointer->VelocityState.NACv = (Payload[2]&0x38)>>3;                    //�������ȼ���
	switch(SubType)
	{

	case 1:
	case 2: 
		{
			InfoPointer->VelocityState.GSInfoAvailable = true ; 


			InfoPointer->VelocityState.Dire_w = (Payload[2]&0x04)>>2	; //���������־
			if(SubType==1)
			{
				InfoPointer->VelocityState.Velocitye_w = ((Payload[2]&0x03)<<8)+Payload[3]-1;
			}
			else
			{
				InfoPointer->VelocityState.Velocitye_w = (((Payload[2]&0x03)<<8)+Payload[3]-1)*4;
			}

			InfoPointer->VelocityState.Dirn_s =(Payload[4]&0x80)>>7;    //�ϱ������־
			if(SubType==1)
			{
				InfoPointer->VelocityState.Velocityn_s = ((((Payload[4]&0x7F)<<8) + ((Payload[5]&0xE0)))>>5)-1;
			}
			else
			{
				InfoPointer->VelocityState.Velocityn_s = (((((Payload[4]&0x7F)<<8) + ((Payload[5]&0xE0)))>>5)-1)*4;
			}

			//��������ٶȺ���
			InfoPointer->VelocityState.GSHeading = TrueTrack(InfoPointer->VelocityState.Velocityn_s,InfoPointer->VelocityState.Velocitye_w,InfoPointer->VelocityState.Dirn_s,InfoPointer->VelocityState.Dire_w);

			if(InfoPointer->VelocityState.Velocityn_s==-1 && InfoPointer->VelocityState.Velocitye_w == -1)
			{
				InfoPointer->VelocityState.GS = 0;
			}	
			else
			{
				InfoPointer->VelocityState.GS  = (sqrt((double)(InfoPointer->VelocityState.Velocityn_s*InfoPointer->VelocityState.Velocityn_s+InfoPointer->VelocityState.Velocitye_w*InfoPointer->VelocityState.Velocitye_w)));
			}

		}
		break;
	case 3:
	case 4: 
		{
			InfoPointer->VelocityState.ASInfoAvailable = true ; 

			InfoPointer->VelocityState.HdgAvailStatusBit =((Payload[2]&0x04)>>2)?true:false; //���к����Ƿ����
			if(InfoPointer->VelocityState.HdgAvailStatusBit) //���Խ��㺽��
			{
				InfoPointer->VelocityState.ASHeading =(((Payload[2]&0x03)<<8) + Payload[3])*360.0/1024.0 ; 

			}
			InfoPointer->VelocityState.TASFlag = ((Payload[4]&0x80)>>7)?true:false;	 //ʹ��IAS����TAS��־
			if(InfoPointer->VelocityState.TASFlag)  
			{
				if(SubType==3)
				{
					InfoPointer->VelocityState.TAS = (  ( ((Payload[4]&0x7F)<<8)   +   ((Payload[5]&0xE0)) ) >>5   )-1 ;//�����
				}
				else
				{
					InfoPointer->VelocityState.TAS =( (  ( ((Payload[4]&0x7F)<<8)   +   ((Payload[5]&0xE0)) ) >>5   )-1 )*4;//�����
				}
			}
			else
			{
				if(SubType==3)
				{
					InfoPointer->VelocityState.IAS = (  ( ((Payload[4]&0x7F)<<8)   +   ((Payload[5]&0xE0)) ) >>5   )-1;//ָʾ����
				}
				else
				{
					InfoPointer->VelocityState.IAS = ((  ( ((Payload[4]&0x7F)<<8)   +   ((Payload[5]&0xE0)) ) >>5   )-1)*4;//ָʾ����
				}
			}

		}
		break;
	}

	InfoPointer->VelocityState.VRateSourceBit = (Payload[5]&0x10)>>4 ; //Source Bit For Vertical Rate
	BYTE c = (Payload[5]&0x08)>>3;
	InfoPointer->VelocityState.CurState = c;//����״̬( 0--���� 1--�½�)

	int i =  (((Payload[5]&0x07)<<8) +(Payload[6]&0xFC))>>2;
	// 	if(i>256)
	// 		i = 9900;
	// 	else
	i=(i-1)*64;

	InfoPointer->VelocityState.VRate = i;
	if(c==1)            //�½�״̬���ٶ�Ϊ��ֵ
		InfoPointer->VelocityState.VRate = InfoPointer->VelocityState.VRate*(-1);


	c = (Payload[7]&0x80)>>7;
	if((Payload[7]&0x7F) ==0)
		InfoPointer->VelocityState.GPSAltDiff=0;
	else
		InfoPointer->VelocityState.GPSAltDiff = ((Payload[7]&0x7F)-1)*25;

	if(c==1)
		InfoPointer->VelocityState.GPSAltDiff = InfoPointer->VelocityState.GPSAltDiff*(-1);
	return TRUE ;


}

///////////////////////////////////ID�����Ϣ����///////////////////////////////////////���صķָ���
const char CharLookup[] =  " ABCDEFGHIJKLMNOPQRSTUVWXYZ                     0123456789     ";

const char* CategoryDescriptions[4][8] = 
{
	{"No ADS-B Emitter Category Information" , 
	"Light (< 15500 lbs)" , 
	"Small (15500 to 75000 lbs)",
	"Large (75000 to 300000 lbs)",
	"High Vortex Large (aircraft such as B-757)",
	"Heavy (> 300000 lbs)",
	"High Performance (> 5g acceleration and 400 kts)",
	"Rotorcraft"},

	{"No ADS-B Emitter Category Information",
	"Glider / sailplane",
	"Lighter-than-air",
	"Parachutist / Skydiver",
	"Ultralight / hang-glider / paraglider",
	"Reserved",
	"Unmanned Aerial Vehicle",
	"Space / Trans-atmospheric vehicle"},

	{"No ADS-B Emitter Category Information",
	"Surface Vehicle �C Emergency Vehicle",
	"Surface Vehicle �C Service Vehicle",
	"Point Obstacle (includes tethered balloons)",
	"Cluster Obstacle",
	"Line Obstacle",
	"Reserved",
	"Reserved"},

	{"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved"
	"Reserved",
	"Reserved"}

} ;

BYTE Convert2Byte(BYTE* pByte,int Index)//ע��:Index����Ϊ1
{

	if(Index%2==1) //����
	{
		return pByte[Index/2];
	}
	else //ż��
	{
		BYTE Ret = 0;
		BYTE a = pByte[Index/2-1]; 
		BYTE b = pByte[Index/2];

		BYTE t1 = a&0x0F;
		Ret = Ret|t1;
		Ret = Ret<<4;
		BYTE t2 = b&0xF0;
		t2 =t2>>4;
		Ret = Ret|t2;
		return Ret;
	}
}
BOOL DecodeIdAndCategoryInfo(DE_HANDLE deHandle ,  // [IN] ���������
	BYTE AdsMessage[14],  // [IN] ADS����Ϣ����
	ADS_Time Time         // [IN] ���Ľ���ʱ��
	)
{
	rwmutex* mutex = GetRWMutex(deHandle)  ;
	Write_Lock(mutex) ; //д����


	LPDBType pDB = H2P(deHandle) ; 

	ICAOAddress  ICAO = ICAOAddress(AdsMessage[1] ,AdsMessage[2] ,AdsMessage[3]) ;
	if(!IsExistICAO(deHandle , ICAO)) //���ݿ��в������˷�������Ŀ
	{
		AircraftInfo Info ; 
		InitAircraftEntry(deHandle , ICAO,Time,&Info) ; 
		AddEntry(deHandle , ICAO ,Info ) ;
	}
	DBPointer IT = pDB->find(ICAO) ; 
	AircraftInfo* InfoPointer = &(IT->second) ; 

	InfoPointer->InfoLastUpdateTime = Time ;  //�������¸���ʱ��
	if(InfoPointer->bTimeOut)
	{
		InfoPointer->bTimeOut = FALSE ; 
	}
	BYTE Code_Type = 	(AdsMessage[4]>>3)&0x1F; //��Ϣ����	
	if(!(Code_Type>=1 && Code_Type<=4)) return FALSE ; 

	//����ԭʼ��6�ֽڵ�ID����
	memcpy(InfoPointer->IdAndCategory.OriFlightID ,&AdsMessage[5] , 6 );

	memset(InfoPointer->IdAndCategory.FlightID,0,8);
	InfoPointer->IdAndCategory.FlightID[0] = CharLookup[Convert2Byte(AdsMessage,11)>>2];
	InfoPointer->IdAndCategory.FlightID[1] = CharLookup[Convert2Byte(AdsMessage,12)%64];
	InfoPointer->IdAndCategory.FlightID[2] = CharLookup[Convert2Byte(AdsMessage,14)>>2];
	InfoPointer->IdAndCategory.FlightID[3] = CharLookup[Convert2Byte(AdsMessage,15)%64];
	InfoPointer->IdAndCategory.FlightID[4] = CharLookup[Convert2Byte(AdsMessage,17)>>2];
	InfoPointer->IdAndCategory.FlightID[5] = CharLookup[Convert2Byte(AdsMessage,18)%64];
	InfoPointer->IdAndCategory.FlightID[6] = CharLookup[Convert2Byte(AdsMessage,20)>>2];
	InfoPointer->IdAndCategory.FlightID[7] = CharLookup[Convert2Byte(AdsMessage,21)%64];

	Code_Type = 	(AdsMessage[4]>>3)&0x1F;
	InfoPointer->IdAndCategory.CategorySet = Code_Type ; 

	InfoPointer->IdAndCategory.Category = AdsMessage[4]&0x07 ; 
	return TRUE ; 
}



const char* GetAircraftCategoryDescription(
	BYTE CategorySet ,  // [IN] �����������(A,B,C,D)
	BYTE Category      // [IN] ��������ϸ���
	)
{
	if(CategorySet>4 || CategorySet<1) return  NULL ;
	if(Category>7) return NULL ;

	return CategoryDescriptions[4-CategorySet][Category] ; 
}

/////////////////////////////////�ɻ�״̬���/////////////////////////////////////////���صķָ���
BOOL DecodeOperationalStatus(DE_HANDLE deHandle ,  // [IN] ���������
	BYTE AdsMessage[14],  // [IN] 112λADS����Ϣ����
	ADS_Time Time         // [IN] ���Ľ���ʱ��
	) 
{
	rwmutex* mutex = GetRWMutex(deHandle)  ;
	Write_Lock(mutex) ; //д����


	LPDBType pDB = H2P(deHandle) ; 

	ICAOAddress  ICAO = ICAOAddress(AdsMessage[1] ,AdsMessage[2] ,AdsMessage[3]) ;
	if(!IsExistICAO(deHandle , ICAO)) //���ݿ��в������˷�������Ŀ
	{
		AircraftInfo Info ; 
		InitAircraftEntry(deHandle , ICAO,Time,&Info) ; 
		AddEntry(deHandle , ICAO ,Info ) ;
	}
	DBPointer IT = pDB->find(ICAO) ; 
	AircraftInfo* InfoPointer = &(IT->second) ; 
	InfoPointer->InfoLastUpdateTime = Time ;  //�������¸���ʱ��
	if(InfoPointer->bTimeOut)
	{
		InfoPointer->bTimeOut = FALSE ; 
	}

	BYTE Code_Type = 	(AdsMessage[4]>>3)&0x1F; //��Ϣ����	
	if(Code_Type!=31) return FALSE ; 

	//Ŀǰֻ�����侫�Ȱ汾��Ϣ
	BYTE Per = (AdsMessage[9]>>5)&0x07 ;
	if(Per==0)
	{
		InfoPointer->Location.LocPercision.PercisionVer = VERSION_ZERO ; 
	}else if(Per==1)
	{
		InfoPointer->Location.LocPercision.PercisionVer = VERSION_ONE ; 
	}
	else
	{

		InfoPointer->Location.LocPercision.PercisionVer = VERSION_ZERO ; 
	}
	InfoPointer->Location.LocPercision.NIC_SUPPLEMENT_A = (AdsMessage[9]>>4)&0x01 ;  //VERSION ==1 ʱNIC����λ

	InfoPointer->Location.LocPercision.NACP = AdsMessage[9]&0x0F ;  //λ�õ������ȼ���
	InfoPointer->StatusInfo.HDR = (AdsMessage[10]>>2)&0x01 ;        //ˮƽ���ο�����

	return TRUE  ;

}

//////////////////////////////////////////////////////////////////////////���صķָ���
size_t CheckTimeOut(DE_HANDLE deHandle,    // [IN] ���������
	std::vector<ICAOAddress>& TimeOutAircrafts, //[OUT] �洢��ʱ��Ŀ���ICAO
	ADS_Time	RefTime ,  //  [IN] ���㳬ʱ�Ĳο�ʱ���
	int TimeOutSeconds,    //  [IN] ��ʱʱ������λ:�룩
	BOOL bSign             //  [IN] �Ƿ��ǳ�ʱ��Ŀ�꣬�����ǣ���ɾ����ʱĿ������Ὣ��ɾ��
	)
{

	rwmutex* mutex = GetRWMutex(deHandle)  ;
	Write_Lock(mutex) ; //д����

	LPDBType pDB = H2P(deHandle) ; 

	if(pDB->empty()) return 0  ; 

	size_t Index = 0 ; 

	DBPointer p = pDB->begin() ; 
	for( ; p!=pDB->end() ; p++)
	{
		time_t T0 = MakeADSTime(p->second.InfoLastUpdateTime) ; 
		time_t T1 = MakeADSTime(RefTime) ; 
		double TimeDiff = difftime(T1, T0) ; 
		int  SecondInterval = static_cast<int>(TimeDiff) ; 

		if(SecondInterval>TimeOutSeconds) 
		{
			if(bSign) p->second.bTimeOut = TRUE; 
			TimeOutAircrafts.push_back(p->second.ICAOID) ; 
			Index++ ; 
		}

	}
	return Index ; 
}
size_t DeleteTimeOut(DE_HANDLE deHandle// [IN] ���������
	) 
{
	rwmutex* mutex = GetRWMutex(deHandle)  ;
	Write_Lock(mutex) ; //д����

	LPDBType pDB = H2P(deHandle) ; 

	if(pDB->empty()) return 0  ; 
	size_t Count = 0 ; 
	DBPointer p = pDB->begin() ; 
	for(;p!=pDB->end() ; )
	{
		if(p->second.bTimeOut)
		{
			pDB->erase(p++) ;
			Count++ ; 
		}
		else
		{
			++p ; 
		}
	}
	return Count ; 
}

size_t DeleteTimeOutFromRefTime(DE_HANDLE deHandle , ADS_Time     RefTime ,  int TimeOutSeconds  )
{

	LPDBType pDB = H2P(deHandle) ;
	if(pDB->empty()) return 0  ;
	size_t Count = 0 ;

	DBPointer p = pDB->begin() ;
	for(   ; p!=pDB->end(); )
	{
		time_t T0 = MakeADSTime(p->second.InfoLastUpdateTime) ;
		time_t T1 = MakeADSTime(RefTime) ;
		double TimeDiff = difftime(T1, T0) ;
		int  SecondInterval = static_cast<int>(TimeDiff) ;

		if(SecondInterval>TimeOutSeconds)
		{
			pDB->erase(p++) ;
			Count++ ;
		}
		else
		{
			++p ;
		}

	}
	return Count ;
}




void VisitAll(DE_HANDLE deHandle,// [IN] ���������
	pfnVisitCallBack fn// [IN] ��������
	)
{
	rwmutex* mutex = GetRWMutex(deHandle)  ;
	Read_Lock(mutex) ; //������

	LPDBType pDB = H2P(deHandle) ; 
	DBPointer p = pDB->begin() ; 
	for(;p!=pDB->end() ;p++ )
	{
		fn(&(p->second)) ; 
	}

}

////////////////////////////////����λ�����//////////////////////////////////////////���صķָ���
//��ȡ�����ƶ��ٶ���Ϣ 
double GetMovement(BYTE MovementByte)
{
	if(MovementByte == 0)
	{
		return 0 ;
	}else if(MovementByte == 1)
	{
		return  0 ;
	}else if(MovementByte == 2)
	{
		return 0.2315 ; 
	}else if(MovementByte>=3 && MovementByte<=8)
	{
		return (MovementByte-3+1)*0.2700833+0.2315 ; 
	}else if(MovementByte>=9 && MovementByte<=12)
	{
		return (MovementByte-9+1)*0.463+1.852 ; 
	}else if(MovementByte>=13 && MovementByte<=38)
	{
		return (MovementByte-13+1)*0.926+3.704 ;
	}else if(MovementByte>=39 && MovementByte<=93)
	{
		return (MovementByte-39+1)*1.852+27.78 ;
	}else if(MovementByte>=94 && MovementByte<=108)
	{
		return (MovementByte-94+1)*3.704+129.64 ;
	}else if(MovementByte>=109 && MovementByte<=123)
	{
		return (MovementByte-109+1)*9.26+185.2 ;
	}else if(MovementByte == 124)
	{
		return 324.1 ; 
	}
	else
	{
		return 0 ;
	}
}


//////////////////////////////////////////////////////////////////////////���صķָ���
BOOL DecodeADSMessage(DE_HANDLE deHandle ,  // [IN] ���������
	BYTE AdsMessage[14], // [IN] ADS��Ϣ����
	ADS_Time Time       // [IN] ���Ľ���ʱ��
	)
{

	BYTE DF_Code = (AdsMessage[0]>>3)&0x1F;   //Ŀǰֻ����DF = 17 ,18 ����Ϣ����
	if(DF_Code!=17 &&DF_Code!=18 )  return FALSE ;

	if(DF_Code==17){

		BYTE Code_Type = (AdsMessage[4]>>3)&0x1F; //��Ϣ����
		switch(Code_Type)
		{
		case 1:
		case 2:
		case 3:
		case 4:
			{

				return DecodeIdAndCategoryInfo(deHandle ,AdsMessage ,Time ) ; 
			}
			break ; 
		case 5:
		case 6:
		case 7:
		case 8:
			{
				return 	DecodeSurfacePostion(deHandle ,AdsMessage ,Time ) ; 
			}
			break ;
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
		case 16:
		case 17:
		case 18:
			{
				return DecodeAirPostion(deHandle ,AdsMessage ,Time ) ; 

			}
			break ; 
		case 19:
			{
				return 	DecodeVelocity(deHandle ,AdsMessage ,Time ) ; 
			}
			break;
		case 20:
		case 21:
		case 22:
			{
				return 	DecodeAirPostion(deHandle ,AdsMessage ,Time ) ; 
			}
			break ; 

		case 31:
			{
				return 	DecodeOperationalStatus(deHandle ,AdsMessage ,Time) ; 
			}
			break ; 

		default:
			return FALSE ; 
		}
	}
	else if( (DF_Code==18) &&( (AdsMessage[0]&0x01)==1 ))
	{
		return 	DecodeSurfacePostion(deHandle ,AdsMessage ,Time ) ; 
	}
	else
		return FALSE ;


}


double real_lat(double lat , double home_lat)
{

	return lat ;

	if(home_lat>0)
		return lat ; 
	else 
		return -90.0 + lat ; 

}


double real_lon(double lon ,double home_lon)
{
	return lon ;

	double diff = 0 ;
	if(lon>=0)
		diff = lon - int(lon/90.0)*90 ; 
	else
	{
		double a = -lon ; 

		diff = (int(a/90.0) +1) *90 -a  ; 
	}

	double rl =0 ; 
	if(home_lon>=0)
	{
		rl =  int(home_lon/90.0)*90.0 + diff ; 

	}
	else{

		rl =  (int(home_lon/90.0)-1)*90.0 + diff ; 

	}
	if(rl>=180)
		rl = rl - 180.0 ; 

	if(rl<=-180.0)
		rl = rl +180.0 ; 

	return rl ; 

}

//////////////////////////////////////////////////////////////////////////���صķָ���
BOOL  DecodeSurfacePostion(DE_HANDLE deHandle ,  // [IN] ���������
	BYTE AdsMessage[14], // [IN] ADS��Ϣ����
	ADS_Time Time       // [IN] ���Ľ���ʱ��
	) 
{

	rwmutex* mutex = GetRWMutex(deHandle)  ;
	Write_Lock(mutex) ; //д����

	BOOL bHavePos =FALSE ; 
	BYTE Code_Type =  (AdsMessage[4]>>3)&0x1F; //��Ϣ����	


	LPDBType pDB = H2P(deHandle) ; 
	ICAOAddress  ICAO = ICAOAddress(AdsMessage[1] ,AdsMessage[2] ,AdsMessage[3]) ;

	if(!IsExistICAO(deHandle , ICAO)) //���ݿ��в������˷�������Ŀ
	{
		AircraftInfo Info ; 
		InitAircraftEntry(deHandle , ICAO,Time,&Info) ; 
		AddEntry(deHandle , ICAO ,Info ) ;
	}

	DBPointer IT = pDB->find(ICAO) ; 
	AircraftInfo* InfoPointer = &(IT->second) ; 

	InfoPointer->InfoLastUpdateTime = Time ;  //�������¸���ʱ��
	if(InfoPointer->bTimeOut)
	{
		InfoPointer->bTimeOut = FALSE ; 
	}

	if(Code_Type>=5 && Code_Type<=8) //�������澭γ����Ϣ
	{

		DWORD NIC = GetNIC(AdsMessage ,0) ; 
		InfoPointer->Location.LocPercision.NIC = NIC ;
		InfoPointer->Location.LocPercision.PercisionVer = VERSION_ONE ; 


		//�ƶ��ٶ���Ϣ
		BYTE MovementByte = ((AdsMessage[4]&0x07)<<4)|((AdsMessage[5]&0xF0)>>4) ;
		InfoPointer->SurfaceState.MoveMentCode = MovementByte ; 
		InfoPointer->SurfaceState.Movement = GetMovement(MovementByte) ; 


		//��ʻ������Ϣ
		BYTE headState = (AdsMessage[5]&0x08)>>3 ; 
		InfoPointer->SurfaceState.HeadingState = headState ; 
		if(headState!=0)
		{
			BYTE HeadCode = ((AdsMessage[5]&0x07)<<4)|((AdsMessage[6]&0xF0)>>4) ; 
			InfoPointer->SurfaceState.Heading = HeadCode*360.0/128.0 ; 
		}

		InfoPointer->Location.LocType = Surface ;  //����Ŀ��
		//����ʱ��ͬ����־λT
		InfoPointer->Location.T =  (AdsMessage[6]>>3)&0x01 ; 

		cpr_decoder* decoder = get_decoder(deHandle);
		assert(decoder);

		ads_cpr_decode_param cpr_param  = get_ads_cpr_param(AdsMessage) ;

		cpr_loc_t loc =   decoder->decode((DWORD)ICAO ,cpr_param.yz ,cpr_param.xz ,cpr_param.cpr_type ,1) ; 

		if(!valid_location(loc))
		{
			bHavePos = FALSE ;
			InfoPointer->Location.LocationAvailable = FALSE  ;
		}
		else
		{
			//���֮ǰ��λ��ʱ���õģ�����Ե�ǰ�����λ�ã���֮ǰ�����λ�õĲ�ֵ�Ƿ�ܴ�
			if(InfoPointer->Location.LocationAvailable) 
			{
				if( (fabs(InfoPointer->Location.EvalLon  - loc.lon  )>2 )   \
					|| (fabs(InfoPointer->Location.EvalLat -  loc.lat) >2) )
				{

					bHavePos = FALSE ;
					InfoPointer->Location.LocationAvailable = FALSE  ;

					decoder->reset((DWORD)ICAO) ;
				}
				else
				{
					InfoPointer->Location.EvalLon =  loc.lon;
					InfoPointer->Location.EvalLat = loc.lat;
					bHavePos = TRUE ;
					InfoPointer->Location.LocationAvailable = TRUE  ;
				}
			}
			else
			{
				InfoPointer->Location.EvalLon =  loc.lon;
				InfoPointer->Location.EvalLat = loc.lat;

				bHavePos = TRUE ;
				InfoPointer->Location.LocationAvailable = TRUE  ;
			}

		}
	}
	InfoPointer->LocationLastUpdateTime =  Time;
	return bHavePos ;
}