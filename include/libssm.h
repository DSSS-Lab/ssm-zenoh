/**
 * @file libssm.h
 * @brief SSM管理用バッグドア関数群
 *
 * ここにある関数・変数にアクセスすると、更新時に互換性が無くなる可能性があるので注意！
 */

#ifndef __LIB_SSM_H__
#define __LIB_SSM_H__

#include <pthread.h>
#include <sys/shm.h>

#include "ssm.h"

#ifndef __GNUC__
#  define  __attribute__(x)  /*NOTHING*/
#endif

	/* ---- defines ---- */

#define SHM_KEY 0x3292							/**< 共有メモリアクセス用キー */
#define SHM_TIME_KEY (SHM_KEY - 1)				/**< 時刻同期用の共有メモリアクセスキー */
#define MSQ_KEY 0x3292							/**< メッセージキューアクセス用キー */
#define ZENOH_MSQ_KEY 0x3293

// #define SHM_NUM 10

/* MessageCommand type */
#define MSQ_CMD 1000
#define MSQ_RES 1001
#define MSQ_RES_MAX 2000

/**
 * @brief メッセージで送るコマンド
 */
enum
{
	MC_NULL = 0,								///< コマンド無し

	MC_VERSION_GET,								///< SSMのバージョンを確認（未実装）

	MC_INITIALIZE,								///< SSMへ接続
	MC_TERMINATE,								///< SSMからの切断

	MC_CREATE,									///< ストリームの作成
	MC_DESTROY,									///< ストリームの破棄（未実装）
	MC_OPEN,									///< ストリームへの接続
	MC_CLOSE,									///< ストリームからの切断(未実装)

	MC_STREAM_PROPERTY_SET,						///< プロパティーの設定
	MC_STREAM_PROPERTY_GET,						///< プロパティーの取得

	MC_GET_TID,									///< TIDの取得(未実装)

	MC_STREAM_LIST_NUM,							///< ストリームの個数を取得
	MC_STREAM_LIST_INFO,						///< ストリーム情報の取得
	MC_STREAM_LIST_NAME,						///< ストリームの名前を取得

	MC_NODE_LIST_NUM,							///< SSMに接続しているノードの数
	MC_NODE_LIST_INFO,							///< ノード情報の取得
	MC_EDGE_LIST_NUM,							///< ノードを繋ぐエッジの数
	MC_EDGE_LIST_INFO,							///< エッジ情報の取得
	
	MC_RES = 31									///< コマンドに対する返信
};

#define SSM_MARGIN 1							///< データアクセスタイミングの余裕 （書き込み中のデータを読まないようにするため）
#define SSM_BUFFER_MARGIN 1.2					///< リングバッファの個数の余裕　（なんとなくあった方が安心？）
#define SSM_TID_SP 0							///< tidの下限

#define SSM_MSG_SIZE  (sizeof(ssm_msg) - sizeof(long) )
// #define SSM_MSG_SIZE (sizeof(ssm_msg))

#ifdef __cplusplus
extern "C"
{
#endif

	/* ---- typedefs ---- */
	/** SSMのヘッダ */
	typedef struct
	{
		SSM_tid tid_top;						///< 最新のTID(rp = tid_top wp = tid_top+1 )
		int num;								///< 履歴数
		size_t size;							///< データサイズ
		double cycle;							///< データの入力される周期（最低値）
		int data_off;							///< データまでのオフセット
		int times_off;							///< 時刻データまでのオフセット
		int sem_lock_id;                        ///< セマフォー用のsemid
		int sem_cond_id;                        ///< セマフォーの条件付きロックのsemid
		int sem_lock_key;
		int sem_cond_key;
		pthread_mutex_t mutex;					///< 同期用mutex lock
		pthread_cond_t cond;					///< 同期用pthread condition
	} ssm_header;

	/** SSMコマンドメッセージ */
	typedef struct
	{
		long msg_type;							///< 宛先
		long res_type;							///< 返信用
		int cmd_type;							///< コマンドの種類
		char name[SSM_SNAME_MAX];				///< ストリーム名
		int suid;								///< ID
		size_t ssize;							///< データサイズ
		size_t hsize;							///< 履歴数
		ssmTimeT time;							///< ストリーム周期
		//** DSSMのための定義を追加 ************************************* ----->
		ssmTimeT saveTime;                       //< saveTime
		//<-----
		//} ssm_msg;
	} __attribute__((__packed__)) ssm_msg;
	
	/** SSMのエッジ取得メッセージ */
	typedef struct
	{
		long msg_type;							///< 宛先
		int cmd_type;							///< コマンドの種類
		char name[SSM_SNAME_MAX];				///< ストリーム名
		int suid;								///< ID
		int node1, node2;						///< エッジにつながっているノード
	} ssm_msg_edge;
	
	/** 時刻同期用の構造体 */
	struct ssmtime
	{
		ssmTimeT offset;						///< 時刻のオフセット
		double speed;							///< 再生速度
		int is_pause;							///< ポーズをしているかどうか 
		ssmTimeT pausetime;						///< ポーズを開始した時刻
	};

	extern struct ssmtime *timecontrol;

	int calcSSM_table( ssmTimeT life, ssmTimeT cycle );
	ssmTimeT calcSSM_life( int table_num, ssmTimeT cycle );

	int getSSM_node_num( void );
	int getSSM_node_info( int n, int *node_num );
	int getSSM_edge_num( void );
	int getSSM_edge_info( int n, char *name, size_t name_size, int *id, int *node1, int *node2, int *dir );
	
	/* open */
	int opentimeSSM( void ) __attribute__ ((warn_unused_result));
	void closetimeSSM( void );
	int createtimeSSM( void ) __attribute__ ((warn_unused_result));
	int destroytimeSSM( void );

	/* shm functions */
	void shm_init_header( ssm_header * header, int ssize, int hsize, ssmTimeT cycle, int sem_key );
	void shm_dest_header( ssm_header * header );
	/* shared memory address */
	ssm_header *shm_get_address( SSM_sid sid );
	/* data */
	void *shm_get_data_address( ssm_header * shm_p );
	void *shm_get_data_ptr( ssm_header * shm_p, SSM_tid tid );
	size_t shm_get_data_size( ssm_header *shm_p );
	/* ssmtime */
	ssmTimeT *shm_get_time_address( ssm_header * shm_p );
	void shm_init_time( ssm_header * shm_p );
	ssmTimeT shm_get_time( ssm_header * shm_p, SSM_tid tid );
	void shm_set_time( ssm_header * shm_p, SSM_tid tid, ssmTimeT time );
	/* tid */
	// SSM_tid *shm_get_timetable_address( ssm_header *shm_p );
	// void shm_init_timetable( ssm_header *shm_p );
	SSM_tid shm_get_tid_top( ssm_header * shm_p );
	SSM_tid shm_get_tid_bottom( ssm_header * shm_p );
	/* mutex */
	int shm_lock( ssm_header * shm_p );
	int shm_unlock( ssm_header * shm_p );
	int shm_cond_wait( ssm_header * shm_p, SSM_tid tid );
	int shm_cond_broadcast( ssm_header * shm_p );
	ssm_header *shm_open_ssm( int shm_id );

	/** ssm time control */
	void inittimeSSM( void );
	int settimeSSM( ssmTimeT time );
	int settimeSSM_speed( double speed );
	double gettimeSSM_speed( void );
	int settimeSSM_is_pause( int is_pause );
	int gettimeSSM_is_pause( void );
	int settimeSSM_is_reverse( int is_reverse );
	int gettimeSSM_is_reverse( void );
	

#ifdef __cplusplus
}
#endif

#endif /*__LIB_SSM_H__*/
