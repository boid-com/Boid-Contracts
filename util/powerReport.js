const api = require('./eosjs')().api
const tapos = { blocksBehind: 6, expireSeconds: 30 }
async function init(data){
  console.log('SetPower',data)
  const result = await api.transact({
      actions: [
        {
          account: 'boidcompower',
          name: 'updaterating',
          authorization: [{actor:data.validator,permission:'active'}],
          data
        }
      ]
    },tapos
  )
  console.log(result.transaction_id)

}

const reports = [
  // {
  //   validator:'boidcompower',
  //   device_name:'0_1061556_5849210',
  //   round_start:259200000000,
  //   round_end:0,
  //   rating:1,
  //   units:1,
  //   protocol_type:0
  // },
  {
    validator:'john.boid',
    device_name:'0_1061556_5849210',
    round_start:259200000000,
    round_end:0,
    rating:10,
    units:1,
    protocol_type:0
  }
]

for (let report of reports) {
  init(report).catch(el => console.log(el.toString()))
}
