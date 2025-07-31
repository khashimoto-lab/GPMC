#include <gmpxx.h>
#include "GPMC/Counter.h"

using namespace GPMC;

/// @brief モデル計数ソルバ Counter の Python 連携用ラッパクラス
///
/// Python から使用するための簡潔なインターフェースを提供する。
/// 節の追加、投射変数の設定、計数モードの指定などが可能。
class CounterWrapper {
public:
    /// @brief コンストラクタ
    /// @details 内部で使用する Instance および構成情報を初期化する。
    CounterWrapper();

    /// @brief 節（節集合）への節の追加
    /// @param c 各リテラルを整数で表現した節（例：{1, -2, 3}）
    void add_clause(std::vector<int> c);

    /// @brief モデル数の計算を実行する
    /// @return モデル数（mpz_class で返す）
    mpz_class count();

    /// @brief 使用する変数数を設定する
    /// @param n 変数の数
    void set_nvars(int n);

    /// @brief 投射変数の指定
    /// @param pvars 投射対象とする変数番号のリスト
    void set_projvars(std::vector<int> pvars);

    /// @brief モデル計数のモード指定
    /// @param mode モード番号（例：0=MC, 1=WMC, 2=PMC, 3=WPMC など）
    void set_mode(int mode);

private:
    /// @brief 節集合を保持するインスタンス
    Instance<mpz_class> ins;

    /// @brief 計数モードや設定を保持する構成情報
    Configuration config;

    /// @brief 最終的な計数結果
    mpz_class res;

    /// @brief 投射変数集合（内部表現：Lit型）
    std::vector<Glucose::Lit> ps;
};
